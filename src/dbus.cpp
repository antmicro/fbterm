#include "dbus.h"

#include <stdio.h>
#include <unistd.h>

#include <memory>

#ifdef DBUS_DEBUG
#define LOG(fmt, args...) fprintf(stderr, "[dbus] " fmt "\n", ##args)
#else
#define LOG(fmt, args...)
#endif

class FbTermDbusWatch : public IoPipe
{
public:
	FbTermDbusWatch(
			FbTermDbus &owner,
			DBusWatch *watch,
			int fd
			)
		: mOwner(owner),
		mWatch(watch)
{
	setFd(fd);
}

	void ready(bool isread) override
	{
		if (!isread)
			return;

		mOwner.handleWatch(
				mWatch,
				DBUS_WATCH_READABLE
				);
	}

	void readyRead(s8 *buf, u32 len) override
	{
		(void)buf;
		(void)len;
	}

private:
	FbTermDbus &mOwner;
	DBusWatch *mWatch;
};

void FbTermDbus::handleWatch(
		DBusWatch *watch,
		unsigned flags
		)
{
	if (!dbus_watch_handle(watch, flags)) {
		fprintf(stderr, "dbus_watch_handle() failed\n");
		return;
	}

	while (dbus_connection_dispatch(mConnection) ==
			DBUS_DISPATCH_DATA_REMAINS) {
	}
}

constexpr const char *INTROSPECTION_XML = R"xml(
<!DOCTYPE node PUBLIC
 "-//freedesktop//DTD D-Bus Object Introspection 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
  <interface name="com.antmicro.fbterm.Display">
    <method name="AcquireLease">
      <arg name="lease_fd" type="h" direction="out"/>
    </method>
    <method name="NotifyLeaseReleased"/>
  </interface>
</node>
)xml";

dbus_bool_t FbTermDbus::AddWatch(DBusWatch *watch)
{
	const int dbus_fd = dbus_watch_get_unix_fd(watch);

	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	// Current integration only supports readable D-Bus watches
	if (!(dbus_watch_get_flags(watch) & DBUS_WATCH_READABLE))
		return TRUE;

	int fd = dup(dbus_fd);
	if (fd == -1) {
		perror("dup");
		return FALSE;
	}

	auto *pipe = new FbTermDbusWatch(*this, watch, fd);
	mWatches.emplace(watch, pipe);

	return TRUE;
}

dbus_bool_t FbTermDbus::AddWatchCallback(
		DBusWatch *watch,
		void *data
		)
{
	auto *self = static_cast<FbTermDbus *>(data);
	return self->AddWatch(watch);
}

void FbTermDbus::RemoveWatchCallback(
        DBusWatch *watch,
        void *data
        )
{
        auto *self = static_cast<FbTermDbus *>(data);
        self->RemoveWatch(watch);
}

void FbTermDbus::ToggleWatchCallback(
        DBusWatch *watch,
        void *data
        )
{
        auto *self = static_cast<FbTermDbus *>(data);
        self->ToggleWatch(watch);
}

void FbTermDbus::RemoveWatch(DBusWatch *watch)
{
        mWatches.erase(watch);
}

void FbTermDbus::ToggleWatch(DBusWatch *watch)
{
}

FbTermDbus::FbTermDbus(DrmDev &drm)
	: mDrm(drm)
{
	DBusError error;
	dbus_error_init(&error);

	mConnection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (!mConnection) {
		fprintf(stderr, "Failed to connect to D-Bus system bus: %s\n",
				dbus_error_is_set(&error) ? error.message : "unknown error");
		dbus_error_free(&error);
		return;
	}

	dbus_connection_set_watch_functions(
			mConnection,
			FbTermDbus::AddWatchCallback,
			FbTermDbus::RemoveWatchCallback,
			FbTermDbus::ToggleWatchCallback,
			this,
			nullptr
			);

	int ret = dbus_bus_request_name(
			mConnection,
			BUS_NAME,
			DBUS_NAME_FLAG_DO_NOT_QUEUE,
			&error
			);

	if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		fprintf(stderr, "Failed to acquire D-Bus name: %s\n",
				dbus_error_is_set(&error) ? error.message : "unknown error");
		dbus_error_free(&error);

		dbus_connection_unref(mConnection);
		mConnection = nullptr;
		return;
	}

	static DBusObjectPathVTable vtable = {
		nullptr,
		messageHandler,
		nullptr,
		nullptr,
		nullptr,
	};

	if (!dbus_connection_register_object_path(
				mConnection,
				OBJECT_PATH,
				&vtable,
				this)) {
		dbus_bus_release_name(mConnection, BUS_NAME, nullptr);
		dbus_connection_unref(mConnection);
		mConnection = nullptr;
		return;
	}

	dbus_connection_set_exit_on_disconnect(mConnection, false);

	dbus_error_free(&error);

	mDrm.setPageFlipCompletedCallback(
			&FbTermDbus::pageFlipCompletedCallback,
			this);
}

FbTermDbus::~FbTermDbus()
{
	mDrm.setPageFlipCompletedCallback(NULL, NULL);

	if (mPendingLeaseRequest != NULL) {
		dbus_message_unref(mPendingLeaseRequest);
		mPendingLeaseRequest = NULL;
		mDrm.resumeRendering();
	}

	if (!mConnection) {
		return;
	}

	dbus_connection_unregister_object_path(mConnection, OBJECT_PATH);
	dbus_bus_release_name(mConnection, BUS_NAME, nullptr);
	dbus_connection_unref(mConnection);
}

DBusHandlerResult FbTermDbus::messageHandler(
		DBusConnection *connection,
		DBusMessage *message,
		void *userData)
{
	auto *self = static_cast<FbTermDbus *>(userData);

	if (dbus_message_is_method_call(
				message,
				"org.freedesktop.DBus.Introspectable",
				"Introspect")) {

		DBusMessage *reply = dbus_message_new_method_return(message);
		if (!reply)
			return DBUS_HANDLER_RESULT_NEED_MEMORY;

		const char *xml = INTROSPECTION_XML;

		if (!dbus_message_append_args(
					reply,
					DBUS_TYPE_STRING,
					&xml,
					DBUS_TYPE_INVALID)) {
			dbus_message_unref(reply);
			return DBUS_HANDLER_RESULT_NEED_MEMORY;
		}

		dbus_connection_send(connection, reply, nullptr);
		dbus_message_unref(reply);

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_method_call(
				message,
				INTERFACE,
				"AcquireLease")) {

		if (self->mPendingLeaseRequest != NULL) {
			DBusMessage *reply = dbus_message_new_error(
					message,
					DBUS_ERROR_FAILED,
					"Another lease request is already pending");

			if (!reply)
				return DBUS_HANDLER_RESULT_NEED_MEMORY;

			dbus_connection_send(connection, reply, nullptr);
			dbus_message_unref(reply);

			return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (self->mDrm.pageFlipPending()) {
			self->mDrm.suspendRendering();
			self->mPendingLeaseRequest = dbus_message_ref(message);

			return DBUS_HANDLER_RESULT_HANDLED;
		}

		self->acquireLeaseAndReply(message);

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_method_call(
				message,
				INTERFACE,
				"NotifyLeaseReleased")) {

		if (!self->mDrm.handleLeaseReleased()) {
                        fprintf(stderr, "drmdev handleLeaseReleased failed!\n");
			DBusMessage *reply = dbus_message_new_error(
					message,
					DBUS_ERROR_FAILED,
					"Failed to handle DRM lease release");

			if (!reply)
				return DBUS_HANDLER_RESULT_NEED_MEMORY;

			dbus_connection_send(connection, reply, nullptr);
			dbus_message_unref(reply);

			return DBUS_HANDLER_RESULT_HANDLED;
		}

		DBusMessage *reply = dbus_message_new_method_return(message);
		if (!reply)
			return DBUS_HANDLER_RESULT_NEED_MEMORY;

		dbus_connection_send(connection, reply, nullptr);
		dbus_message_unref(reply);

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void FbTermDbus::acquireLeaseAndReply(DBusMessage *message)
{
	int lease_fd = -1;

	if (!mDrm.acquireLease(lease_fd)) {
		DBusMessage *reply = dbus_message_new_error(
				message,
				DBUS_ERROR_FAILED,
				"Failed to create DRM lease");

		if (reply != NULL) {
			dbus_connection_send(mConnection, reply, NULL);
			dbus_message_unref(reply);
		} else {
			LOG("Failed to allocate DRM lease error reply");
		}

		return;
	}

	DBusMessage *reply = dbus_message_new_method_return(message);
	if (reply == NULL) {
		LOG("Failed to allocate DRM lease reply");
		if (!mDrm.releaseLease(lease_fd))
			LOG("Failed to roll back DRM lease");
		return;
	}

	DBusMessageIter iter;
	dbus_message_iter_init_append(reply, &iter);

	DBusBasicValue value;
	value.fd = lease_fd;

	if (!dbus_message_iter_append_basic(
				&iter,
				DBUS_TYPE_UNIX_FD,
				&value)) {
		LOG("Failed to append DRM lease fd to reply");
		dbus_message_unref(reply);
		if (!mDrm.releaseLease(lease_fd))
			LOG("Failed to roll back DRM lease");
		return;
	}

	if (!dbus_connection_send(mConnection, reply, NULL)) {
		dbus_message_unref(reply);
		if (!mDrm.releaseLease(lease_fd))
			LOG("Failed to roll back DRM lease");
		return;
	}

	dbus_message_unref(reply);

	close(lease_fd);
}

void FbTermDbus::pageFlipCompleted()
{
	if (mPendingLeaseRequest == NULL)
		return;

	DBusMessage *message = mPendingLeaseRequest;
	mPendingLeaseRequest = NULL;

	acquireLeaseAndReply(message);
	dbus_message_unref(message);
}

void FbTermDbus::pageFlipCompletedCallback(void *user_data)
{
	FbTermDbus *self = static_cast<FbTermDbus *>(user_data);
	self->pageFlipCompleted();
}

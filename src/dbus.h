#ifndef FBTERM_DBUS_H
#define FBTERM_DBUS_H

#include <dbus/dbus.h>
#include "lib/io.h"
#include "drmdev.h"

#include <map>
#include <memory>

class FbTermDbusWatch;

class FbTermDbus {
public:
	FbTermDbus(DrmDev &drm);
	~FbTermDbus();

	bool valid() const { return mConnection != nullptr; }

	void handleWatch(DBusWatch *watch, unsigned flags);

private:

	static DBusHandlerResult messageHandler(
			DBusConnection *connection,
			DBusMessage *message,
			void *userData
			);

	static dbus_bool_t AddWatchCallback(
			DBusWatch *watch,
			void *data
			);

	static void RemoveWatchCallback(
			DBusWatch *watch,
			void *data
			);

	static void ToggleWatchCallback(
			DBusWatch *watch,
			void *data
			);

	dbus_bool_t AddWatch(DBusWatch *watch);
	void RemoveWatch(DBusWatch *watch);
	void ToggleWatch(DBusWatch *watch);

	DrmDev &mDrm;

	void acquireLeaseAndReply(DBusMessage *message);
	static void pageFlipCompletedCallback(void *user_data);
	void pageFlipCompleted();
	DBusMessage *mPendingLeaseRequest = NULL;

	DBusConnection *mConnection = nullptr;

	std::map<DBusWatch *, FbTermDbusWatch *> mWatches;

	static constexpr const char *BUS_NAME = "com.antmicro.fbterm";
	static constexpr const char *OBJECT_PATH = "/com/antmicro/fbterm";
	static constexpr const char *INTERFACE = "com.antmicro.fbterm.Display";
};

#endif // FBTERM_DBUS_H

#ifndef MOUSE_GENERIC_H
#define MOUSE_GENERIC_H

#include "instance.h"
#include "io.h"

class GenericMouse : public IoPipe {
	DECLARE_INSTANCE(GenericMouse)
public:
	virtual void switchVc(bool enter) {}
};

#endif

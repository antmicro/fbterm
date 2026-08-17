#ifndef INPUT_GENERIC_H
#define INPUT_GENERIC_H

#include "instance.h"
#include "io.h"

class KBInput : public IoPipe {
	DECLARE_INSTANCE(KBInput)
public:
	virtual void switchVc(bool enter) {}
	virtual void setRawMode(bool raw, bool force = false) {}
	virtual void showInfo(bool verbose) {}
};

#endif

#include "Link.h"
#include "Structs.h"

bool Link::IsDirectory() {
	auto attributes = this->attributes.Attributes();
	return attributes->IsDirectory();
}

// TODO
#include "Collectable.h"

namespace GC {
	ScanLists* ScanListsByThread[MAX_COLLECTED_THREADS];
	int ActiveIndex;
}

void InstancePtrBase::mark()
{
    Collectable* s = get_collectable();
    if (s != nullptr) s->collectable_mark();
}

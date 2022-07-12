#include "Collectable.h"

namespace GC {
	ScanLists* ScanListsByThread[MAX_COLLECTED_THREADS];
	int ActiveIndex;
}
#pragma once

#ifndef SLC_STARLANE_CORE_H
#define SLC_STARLANE_CORE_H

namespace Starlane {
void InitBackend();
void TimeTick();
}

namespace SLFrontend {
void FatalError(const char *msg);
}

#endif  // !SLC_STARLANE_CORE_H
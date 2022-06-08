#include "gameobj.h"

namespace Starlane {

GameObj *GameObj::Clone() const {
	return new GameObj(*this);
}

}
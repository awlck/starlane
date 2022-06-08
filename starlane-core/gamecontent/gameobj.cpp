#include "gameobj.h"

#include "../game.h"
#include "group.h"

namespace Starlane {

GameObj *GameObj::Clone() const {
	return new GameObj(*this);
}

}
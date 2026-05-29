#include "movement.h"

#include "fly/fly.h"
#include "speed/speed.h"
#include "tickrate/tickrate.h"

#include <thread>

void movement::run()
{
	std::thread(fly::run).detach();
	std::thread(speed::run).detach();
	tickrate::run();
}

#pragma once
namespace slowtime
{
	void Tick();   // every read: the world slows while the radial or the ammo wheel is open
	void CloseRequested(); // game thread: restore before the slowed close animation
	void Reopened();
}

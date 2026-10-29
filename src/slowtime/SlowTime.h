#pragma once
namespace slowtime
{
	void Tick();
	void CloseRequested(); // game thread: restore before the slowed close animation
	void Reopened();
}

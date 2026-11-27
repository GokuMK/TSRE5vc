#pragma once

namespace TsreTests {
// Task explore 06: the wait in walking runs, through its keys and clicks.
// On --route PRUDNIK (PSK_2026): on foot at Raszowa before 0518_R46403
// comes, T, the dialog and Esc; Next train (its end 30 s before the train
// arrives); the train boarded, Next stop (the same before its next stop);
// a wait for a time (to the second), Stop half way, Skip (the world not
// drawn until the end), a server's pace (the run at its own); the game
// menu's Wait; at Prudnik with no trains, Next train greyed. On --route
// EUROPE1: lclsrvce on foot at its second stop, Next train and a wait for a
// time. Needs --game-root and --route (none: nothing checked) and a GL
// surface.
int runExploreWaitSuite(bool verbose);
}

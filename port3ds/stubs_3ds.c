//
// Stubs for features the 3DS build leaves out: the ENDOOM text screen
// (i_endoom.c) and the multiplayer launch GUI (net_gui.c), both of which
// need the textscreen library.
//

#include "doomtype.h"
#include "i_endoom.h"
#include "net_gui.h"

void I_Endoom(byte *endoom_data)
{
    (void) endoom_data;
}

void NET_WaitForLaunch(void)
{
}

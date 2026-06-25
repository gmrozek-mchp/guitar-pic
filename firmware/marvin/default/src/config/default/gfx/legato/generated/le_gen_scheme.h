#ifndef LEGATO_INIT_SCHEME_H
#define LEGATO_INIT_SCHEME_H

#include "gfx/legato/legato.h"

extern const leScheme WhiteScheme;
extern const leScheme RedScheme;
extern const leScheme GreenScheme;
extern const leScheme BlueScheme;
extern const leScheme YellowScheme;
extern const leScheme OrangeScheme;
extern const leScheme panel_white;
extern const leScheme SCHEME_BACKGROUND;
extern const leScheme panel_darkslategray;
extern const leScheme SCHEME_TEXT_WHITE;
extern const leScheme SCHEME_TEXT_DIM_GRAY;
extern const leScheme panel_limegreen;
extern const leScheme SCHEME_PANEL;
extern const leScheme text_darkgray;
extern const leScheme SCHEME_GUITAR_FRET_GREEN;
extern const leScheme SCHEME_NAV_BUTTON;
extern const leScheme panel_darkgoldenrod;
extern const leScheme text_black;
extern const leScheme text_limegreen;
extern const leScheme panel_darkslategray_0;
extern const leScheme text_crimson;
extern const leScheme panel_crimson;
extern const leScheme panel_dimgray;
extern const leScheme SCHEME_GUITAR_FRET_RED;
extern const leScheme SCHEME_GUITAR_FRET_ORANGE;
extern const leScheme panel_darkorange;
extern const leScheme text_gainsboro;
extern const leScheme panel_dodgerblue_0;
extern const leScheme panel_mediumseagreen;
extern const leScheme panel_royalblue;
extern const leScheme panel_orange_0;
extern const leScheme panel_darkorange_0;
extern const leScheme panel_tomato;
extern const leScheme panel_mediumorchid;
extern const leScheme text_lightgray;
extern const leScheme SCHEME_GUITAR_FRET_BLUE;
extern const leScheme SCHEME_NAV_MENU_BUTTON;

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

void legato_initialize_schemes(void);

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LEGATO_INIT_SCHEME_H

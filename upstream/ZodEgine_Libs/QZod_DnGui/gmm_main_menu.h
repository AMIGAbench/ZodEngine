#ifndef ZGMM_MAIN_MENU_H
#define ZGMM_MAIN_MENU_H

#include "qzod_dngui_global.h"
#include "zgui_main_menu_base.h"
#include "gui_structures.h"


class QZOD_DNGUISHARED_EXPORT GMMMainMenu : public ZGuiMainMenuBase
{
public:
	GMMMainMenu();

	//Die Rueckfrage vor dem Beenden -- EINE Quelle fuer beide Wege, die
	//dorthin fuehren: der Knopf "Quit Game" in diesem Menue und die
	//ESC-Taste (ZPlayer::keydown_event). Stuende der Text an beiden
	//Stellen, koennten sie auseinanderlaufen.
	static gmm_warning_flag QuitWarningFlags();
private:
	GMMWButton menu_button[MAX_GMMMM_BUTTONS];

	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);
};

#endif

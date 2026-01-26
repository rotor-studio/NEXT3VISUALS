#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

	//Use ofGLFWWindowSettings for more options like multi-monitor fullscreen
	ofGLWindowSettings settings;
	const int monitorWidth = 1080 / 3;
	const int monitorHeight = (1920 * 2) / 3;
	const int uiSidebarWidth = 260;
	settings.setSize(monitorWidth + uiSidebarWidth, monitorHeight);
	settings.windowMode = OF_WINDOW; //can also be OF_FULLSCREEN

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

}

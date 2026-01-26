#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxNDIreceiver.h"
#include "ofxNDIsender.h"
#include <array>

class ofApp : public ofBaseApp {

public:
	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseScrolled(int x, int y, float scrollX, float scrollY);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

private:
	void refreshNdiSenders();
	void selectSenderIndex(int index);
	void drawNdiDropdown();
	void drawTextureFitted(const ofTexture &texture, const ofRectangle &bounds);
	void updatePreviewRect();
	void drawFoamControls();
	void drawParticleControls();
	void createFoamLayer();
	void removeSelectedFoamLayer();
	void updateFoamLayers();
	void resetComposition();
	void createParticleSystem();
	void removeSelectedParticleSystem();
	void updateParticles(float dt);
	void drawParticles();
	void updateSelectedParticleSlider(float mouseX);
	void drawSliderLabel(const std::string &label, const ofRectangle &rect) const;
	void saveComposition();
	void loadComposition();
	bool hitTestFoamLayer(const ofVec2f &outputPos, int &hitIndex) const;
	ofVec2f windowToOutput(const ofVec2f &windowPos) const;
	ofRectangle outputToWindowRect(const ofRectangle &outputRect) const;
	void updateSelectedFoamFade(float mouseX);
	void updateNdiPlacement();
	ofRectangle getNdiOutputRect() const;
	void drawNdiTestPattern();
	float getUiSidebarWidth() const;

	ofxNDIreceiver ndiReceiver;
	ofxNDIsender ndiSender;
	std::string ndiOutputName = "NN3_COMPOSITE";
	ofTexture ndiTexture;
	ofPixels ndiPixels;
	ofPixels ndiPixelsPrev;
	bool ndiEnabled = true;
	float ndiFade = 1.0f;
	float maskCaptureInterval = 1.0f / 15.0f;
	float lastMaskCaptureTime = -1.0f;
	bool maskPixelsReady = false;
	ofFbo outputFbo;
	int outputWidth = 1080;
	int outputHeight = 3840;
	std::vector<std::string> ndiSenders;
	int selectedSenderIndex = -1;
	float lastSenderScanTime = 0.0f;
	float senderScanInterval = 2.0f;

	ofxPanel gui;
	bool showGui = true;

	ofRectangle dropdownRect;
	float dropdownItemHeight = 22.0f;
	bool dropdownOpen = false;

	ofRectangle previewRect;
	bool previewRectInitialized = false;

	ofVec2f ndiPosition;
	ofVec2f ndiSize;
	bool ndiPlacementInitialized = false;
	bool draggingNdi = false;
	ofVec2f ndiDragOffset;

	struct FoamLayer {
		ofFbo fbo;
		ofVec2f position;
		ofVec2f size;
		float timeOffset = 0.0f;
		float fade = 0.85f;
		bool useMist = false;
		bool enabled = true;
		bool locked = false;
	};

	ofShader foamShader;
	ofShader mistShader;
	std::vector<FoamLayer> foamLayers;
	int selectedFoamIndex = -1;
	bool draggingFoam = false;
	ofVec2f foamDragOffset;
	ofTrueTypeFont ndiTestFont;

	ofRectangle addFoamRect;
	ofRectangle deleteFoamRect;
	ofRectangle fadeSliderRect;
	ofRectangle mistSpeedRect;
	ofRectangle resetRect;
	ofRectangle ndiTestRect;
	bool draggingFade = false;
	bool draggingMistSpeed = false;
	bool resetArmed = false;
	float resetArmedTime = 0.0f;
	bool showNdiTestPattern = false;
	ofRectangle foamEnableRect;
	ofRectangle foamMistRect;
	bool foamGroupEnabled = true;
	bool foamUseMist = false;
	float mistSpeed = 1.0f;

	bool showAllBorders = false;

	struct Particle {
		static constexpr int kTrailPoints = 64;
		ofVec2f position;
		ofVec2f velocity;
		ofVec2f prevPos;
		ofVec2f trailPos;
		ofVec2f prevTrailPos;
		ofVec2f trailDelta;
		std::array<ofVec2f, kTrailPoints> trail;
		int trailHead = 0;
		int trailCount = 0;
		float age = 0.0f;
		float lifespan = 1.0f;
		float noiseSeed = 0.0f;
	};

	struct ParticleSystem {
		ofRectangle emitterRect;
		std::vector<Particle> particles;
		float spawnRate = 25.0f;
		float spawnAccumulator = 0.0f;
		float size = 3.0f;
		float speed = 220.0f;
		float bounce = 0.5f;
		float maxParticles = 250.0f;
		float lifeSpanMin = 1.0f;
		float lifeSpanMax = 4.0f;
		float trail = 0.0f;
		float fade = 1.0f;
		float noise = 0.0f;
		float noiseStart = 0.5f;
		bool enabled = true;
		bool locked = false;
	};

	std::vector<ParticleSystem> particleSystems;
	int selectedParticleIndex = -1;
	bool draggingParticleSlider = false;
	int activeParticleSlider = -1;
	bool draggingEmitter = false;
	ofVec2f emitterDragOffset;

	ofRectangle addParticleRect;
	ofRectangle deleteParticleRect;
	ofRectangle particleSizeRect;
	ofRectangle particleSpeedRect;
	ofRectangle particleBounceRect;
	ofRectangle particleAmountRect;
	ofRectangle particleLifeRect;
	ofRectangle particleTrailRect;
	ofRectangle particleFadeRect;
	ofRectangle particleNoiseRect;
	ofRectangle particleNoiseStartRect;
	ofRectangle particleEnableRect;
	ofRectangle ndiEnableRect;
	ofRectangle ndiFadeRect;
	bool particleGroupEnabled = true;
	bool draggingNdiFade = false;

	enum class LayerSelection {
		None,
		NDI,
		Foam,
		Particles
	};

	LayerSelection selectedLayer = LayerSelection::None;

	std::string compositionPath = "presets/composition.json";
};

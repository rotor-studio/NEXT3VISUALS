#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetWindowTitle("ROTOR STUDIO - NN3");

	ndiReceiver.SetAudio(false);
	ndiReceiver.SetUpload(false);
	ndiReceiver.SetLowBandwidth(false);
	ndiReceiver.SetFormat(NDIlib_recv_color_format_BGRX_BGRA);
	ndiReceiver.CreateFinder();

	ndiTexture.allocate(2, 2, GL_RGBA);

	outputFbo.allocate(outputWidth, outputHeight, GL_RGBA);
	outputFbo.begin();
	ofClear(0, 0, 0, 255);
	outputFbo.end();

	std::string foamShaderPath;
	std::string mistShaderPath;
	if (ofIsGLProgrammableRenderer()) {
		foamShaderPath = "shaders/foam/GL3/foam";
		mistShaderPath = "shaders/mist/GL3/mist";
	} else {
		foamShaderPath = "shaders/foam/GL2/foam";
		mistShaderPath = "shaders/mist/GL2/mist";
	}
	if (!foamShader.load(foamShaderPath)) {
		ofLogWarning() << "Failed to load foam shader: " << foamShaderPath;
	}
	if (!mistShader.load(mistShaderPath)) {
		ofLogWarning() << "Failed to load mist shader: " << mistShaderPath;
	}

	gui.setup("CONFIG");
	gui.setPosition(10.0f, 10.0f);
	refreshNdiSenders();

	ndiTestFont.load("fonts/arial.ttf", 64, true, true);
	logoImage.load("images/logo.png");

	ndiSender.CreateSender(ndiOutputName.c_str(), outputWidth, outputHeight);
	oscSender.setup(oscHost, oscPort);
	loadComposition();
}

//--------------------------------------------------------------
void ofApp::update(){
	const float now = ofGetElapsedTimef();
	if (now - lastSenderScanTime >= senderScanInterval) {
		refreshNdiSenders();
	}
	if (resetArmed && (now - resetArmedTime) > 2.0f) {
		resetArmed = false;
	}

	if (selectedSenderIndex > 0 && selectedSenderIndex < static_cast<int>(ndiSenders.size())) {
		if (selectedSenderIndex <= ndiAvailableSenderCount && !ndiReceiver.ReceiverCreated()) {
			ndiReceiver.SetSenderName(ndiSenders[selectedSenderIndex]);
			ndiReceiver.CreateReceiver(-1);
		}
	}

	if (ndiReceiver.ReceiverCreated()) {
		ndiReceiver.ReceiveImage(ndiTexture);
		const bool needsMask = particleGroupEnabled && !particleSystems.empty();
		if (ndiEnabled && ndiReceiver.ReceiverConnected() && ndiTexture.isAllocated() && needsMask) {
			if (lastMaskCaptureTime < 0.0f || (now - lastMaskCaptureTime) >= maskCaptureInterval) {
				ofPixels tempPixels;
				ndiTexture.readToPixels(tempPixels);
				if (tempPixels.isAllocated() && tempPixels.getNumChannels() >= 3) {
					if (ndiPixels.isAllocated()) {
						ndiPixelsPrev = ndiPixels;
					}
					ndiPixels = tempPixels;
					if (!ndiPixelsPrev.isAllocated()) {
						ndiPixelsPrev = ndiPixels;
					}
					maskPixelsReady = true;
				} else {
					maskPixelsReady = false;
				}
				lastMaskCaptureTime = now;
			}
		}
	}

	updateNdiPlacement();
	const float dt = ofGetLastFrameTime();
	updatePresetTransition(dt);
	updateFoamLayers();
	updateParticles(dt);

	if (cyclePlaying && cycleDuration > 0.0f) {
		cyclePhasePrev = cyclePhase;
		cyclePhase += dt / cycleDuration;
		if (cyclePhase >= 1.0f) {
			cyclePhase -= 1.0f;
			cyclePhasePrev = cyclePhase;
			cycleTriggered = false;
		}
		const float triggerStart = ofClamp(1.0f - cycleTriggerWidth, 0.0f, 1.0f);
		const bool crossedIntoWindow = (!cycleTriggered &&
			((cyclePhasePrev < triggerStart && cyclePhase >= triggerStart) ||
			 (cyclePhasePrev > cyclePhase && cyclePhase >= triggerStart)));
		if (cycleTriggerWidth > 0.0f && crossedIntoWindow) {
			triggerCycleEvent();
			cycleTriggered = true;
		}
	}

	if (outputFbo.isAllocated()) {
		outputFbo.begin();
		ofClear(0, 0, 0, 255);
		if (showNdiTestPattern) {
			drawNdiTestPattern();
		}
		const float compositeAlpha = presetTransitionAlpha;
		if (ndiEnabled && ndiTexture.isAllocated() && ndiReceiver.ReceiverConnected()) {
			const ofRectangle ndiRect = getNdiOutputRect();
			ofDisableAlphaBlending();
			ofSetColor(255, 255, 255, static_cast<unsigned char>(ndiFade * compositeAlpha * 255.0f));
			ndiTexture.draw(ndiRect);
		}
		drawParticles();
		ofEnableAlphaBlending();
		if (foamGroupEnabled) {
			for (const auto &layer : foamLayers) {
				if (layer.fbo.isAllocated() && layer.enabled) {
					ofSetColor(255, 255, 255, static_cast<unsigned char>(layer.fade * compositeAlpha * 255.0f));
					layer.fbo.draw(layer.position.x, layer.position.y, layer.size.x, layer.size.y);
				}
			}
		}
		outputFbo.end();

		if (ndiSender.SenderCreated()) {
			ndiSender.SendImage(outputFbo);
		}
	}

	updatePreviewRect();

}

//--------------------------------------------------------------
void ofApp::draw(){
	ofBackground(0);

	if (previewEnabled) {
		if (outputFbo.isAllocated()) {
			ofSetColor(255);
			outputFbo.getTexture().draw(previewRect);
		}
		if (!showNdiTestPattern && previewRect.getWidth() > 0.0f && previewRect.getHeight() > 0.0f) {
			ofNoFill();
			ofSetColor(255);
			ofSetLineWidth(1.5f);
			ofDrawRectangle(previewRect);
			ofFill();
		}
	}

	if (previewEnabled && draggingFoam && selectedFoamIndex >= 0 && selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		const FoamLayer &layer = foamLayers[selectedFoamIndex];
		const ofRectangle outputRect(layer.position.x, layer.position.y, layer.size.x, layer.size.y);
		const ofRectangle windowRect = outputToWindowRect(outputRect);
		ofNoFill();
		ofSetColor(0, 120, 255);
		ofSetLineWidth(2.0f);
		ofDrawRectangle(windowRect);
		ofFill();
	}

	if (previewEnabled && draggingNdi && ndiTexture.isAllocated()) {
		const ofRectangle ndiRect = getNdiOutputRect();
		const ofRectangle windowRect = outputToWindowRect(ndiRect);
		ofNoFill();
		ofSetColor(0, 120, 255);
		ofSetLineWidth(2.0f);
		ofDrawRectangle(windowRect);
		ofFill();
	}

	if (previewEnabled && !draggingFoam && selectedLayer == LayerSelection::Foam && selectedFoamIndex >= 0 &&
		selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		const FoamLayer &layer = foamLayers[selectedFoamIndex];
		const ofRectangle outputRect(layer.position.x, layer.position.y, layer.size.x, layer.size.y);
		const ofRectangle windowRect = outputToWindowRect(outputRect);
		ofNoFill();
		ofSetColor(255, 180, 0);
		ofSetLineWidth(2.0f);
		ofDrawRectangle(windowRect);
		ofFill();
		ofSetColor(255, 180, 0);
		ofDrawBitmapString("X", windowRect.x + 4.0f, windowRect.y + 12.0f);
		if (layer.locked) {
			ofSetColor(255, 180, 0);
		} else {
			ofSetColor(120);
		}
		ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
	}

	if (previewEnabled && !draggingNdi && selectedLayer == LayerSelection::NDI && ndiTexture.isAllocated()) {
		const ofRectangle ndiRect = getNdiOutputRect();
		const ofRectangle windowRect = outputToWindowRect(ndiRect);
		ofNoFill();
		ofSetColor(255, 180, 0);
		ofSetLineWidth(2.0f);
		ofDrawRectangle(windowRect);
		ofFill();
	}

	if (previewEnabled && selectedLayer == LayerSelection::Particles && selectedParticleIndex >= 0 &&
		selectedParticleIndex < static_cast<int>(particleSystems.size())) {
		const ofRectangle windowRect = outputToWindowRect(particleSystems[selectedParticleIndex].emitterRect);
		ofNoFill();
		ofSetColor(255, 180, 0);
		ofSetLineWidth(2.0f);
		ofDrawRectangle(windowRect);
		ofFill();
		ofSetColor(255, 180, 0);
		ofDrawBitmapString("X", windowRect.x + 4.0f, windowRect.y + 12.0f);
		if (particleSystems[selectedParticleIndex].locked) {
			ofSetColor(255, 180, 0);
		} else {
			ofSetColor(120);
		}
		ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
	}

	if (previewEnabled && showAllBorders) {
		if (ndiTexture.isAllocated()) {
			const ofRectangle ndiRect = getNdiOutputRect();
			const ofRectangle windowRect = outputToWindowRect(ndiRect);
			ofNoFill();
			if (selectedLayer == LayerSelection::NDI) {
				ofSetColor(255, 180, 0);
			} else {
				ofSetColor(0, 120, 255);
			}
			ofSetLineWidth(1.5f);
			ofDrawRectangle(windowRect);
			ofFill();
			if (selectedLayer == LayerSelection::NDI) {
				ofSetColor(255, 180, 0);
				ofDrawBitmapString("X", windowRect.x + 4.0f, windowRect.y + 12.0f);
			}
		}

		for (const auto &layer : foamLayers) {
			const ofRectangle outputRect(layer.position.x, layer.position.y, layer.size.x, layer.size.y);
			const ofRectangle windowRect = outputToWindowRect(outputRect);
			ofNoFill();
			if (selectedLayer == LayerSelection::Foam && selectedFoamIndex >= 0 &&
				&layer == &foamLayers[selectedFoamIndex]) {
				ofSetColor(255, 180, 0);
			} else {
				ofSetColor(0, 120, 255);
			}
			ofSetLineWidth(1.0f);
			ofDrawRectangle(windowRect);
			ofFill();
			if (selectedLayer == LayerSelection::Foam && selectedFoamIndex >= 0 &&
				&layer == &foamLayers[selectedFoamIndex]) {
				ofSetColor(255, 180, 0);
				ofDrawBitmapString("X", windowRect.x + 4.0f, windowRect.y + 12.0f);
				if (layer.locked) {
					ofSetColor(255, 180, 0);
				} else {
					ofSetColor(120);
				}
				ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
			} else {
				ofSetColor(0, 120, 255);
				if (layer.locked) {
					ofSetColor(255, 180, 0);
				}
				ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
			}
		}

		for (const auto &system : particleSystems) {
			const ofRectangle windowRect = outputToWindowRect(system.emitterRect);
			ofNoFill();
			if (selectedLayer == LayerSelection::Particles && selectedParticleIndex >= 0 &&
				&system == &particleSystems[selectedParticleIndex]) {
				ofSetColor(255, 180, 0);
			} else {
				ofSetColor(0, 120, 255);
			}
			ofSetLineWidth(1.0f);
			ofDrawRectangle(windowRect);
			ofFill();
			if (selectedLayer == LayerSelection::Particles && selectedParticleIndex >= 0 &&
				&system == &particleSystems[selectedParticleIndex]) {
				ofSetColor(255, 180, 0);
				ofDrawBitmapString("X", windowRect.x + 4.0f, windowRect.y + 12.0f);
				if (system.locked) {
					ofSetColor(255, 180, 0);
				} else {
					ofSetColor(120);
				}
				ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
			} else {
				ofSetColor(0, 120, 255);
				if (system.locked) {
					ofSetColor(255, 180, 0);
				}
				ofDrawBitmapString("L", windowRect.x + windowRect.width - 12.0f, windowRect.y + 12.0f);
			}
		}
	}

	if (showGui) {
		ofSetColor(255);
		gui.draw();
		drawFoamControls();
		drawParticleControls();
		drawNdiDropdown();
	}

	const float fpsX = previewRect.x + 8.0f;
	const float fpsY = previewRect.y + 18.0f;
	ofSetColor(0, 0, 0, 160);
	ofDrawRectangle(fpsX - 4.0f, fpsY - 14.0f, 70.0f, 18.0f);
	ofSetColor(220);
	ofDrawBitmapString("FPS " + ofToString(ofGetFrameRate(), 1), fpsX, fpsY);

	if (compactMode && previewRect.getWidth() > 0.0f) {
		const float barW = 140.0f;
		const float barH = 6.0f;
		const float barX = previewRect.x + previewRect.getWidth() - barW - 8.0f;
		const float barY = fpsY - barH - 6.0f;
		ofSetColor(0, 0, 0, 160);
		ofDrawRectangle(barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f);
		ofSetColor(60);
		ofDrawRectangle(barX, barY, barW, barH);
		ofSetColor(180);
		ofDrawRectangle(barX, barY, barW * ofClamp(cyclePhase, 0.0f, 1.0f), barH);
	}

	if (logoImage.isAllocated()) {
		const float logoSize = 75.0f;
		const float logoX = 8.0f;
		const float logoY = static_cast<float>(ofGetHeight()) - logoSize - 8.0f;
		ofSetColor(255);
		logoImage.draw(logoX, logoY, logoSize, logoSize);
	}

}

//--------------------------------------------------------------
void ofApp::exit(){
	ndiReceiver.ReleaseFinder();
	ndiReceiver.ReleaseReceiver();
	ndiSender.ReleaseSender();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	if (key == 'g' || key == 'G') {
		showGui = !showGui;
	}
	if (key == 'k' || key == 'K') {
		configLocked = !configLocked;
	}
	if (key == 'p' || key == 'P') {
		compactMode = !compactMode;
		if (compactMode) {
			compactSavedWindowSize.set(ofGetWidth(), ofGetHeight());
			compactSavedGui = showGui;
			compactSavedPreview = previewEnabled;
			const float scale = 0.75f;
			const float aspect = static_cast<float>(outputWidth) / static_cast<float>(outputHeight);
			const int newH = static_cast<int>(compactSavedWindowSize.y * scale);
			const int newW = static_cast<int>(newH * aspect);
			ofSetWindowShape(std::max(200, newW), std::max(200, newH));
			showGui = false;
			previewEnabled = true;
			showAllBorders = false;
		} else {
			if (compactSavedWindowSize.x > 0.0f && compactSavedWindowSize.y > 0.0f) {
				ofSetWindowShape(static_cast<int>(compactSavedWindowSize.x),
					static_cast<int>(compactSavedWindowSize.y));
			}
			showGui = compactSavedGui;
			previewEnabled = compactSavedPreview;
		}
	}
	if (key == 'v' || key == 'V') {
		showAllBorders = !showAllBorders;
	}
	if (key == 's' || key == 'S') {
		saveComposition();
	}
	if (key == 'l' || key == 'L') {
		loadComposition();
	}

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int    x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
	if (configLocked || compactMode) {
		return;
	}
	if (draggingFoam && selectedFoamIndex >= 0 && selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		const ofVec2f outputPos = windowToOutput(ofVec2f(x, y));
		FoamLayer &layer = foamLayers[selectedFoamIndex];
		if (layer.locked) {
			return;
		}
		layer.position.x = outputPos.x - foamDragOffset.x;
		layer.position.y = outputPos.y - foamDragOffset.y;

		layer.position.x = ofClamp(layer.position.x, -layer.size.x, static_cast<float>(outputWidth));
		layer.position.y = ofClamp(layer.position.y, 0.0f, outputHeight - layer.size.y);
	}

	if (draggingNdi && ndiTexture.isAllocated()) {
		const ofVec2f outputPos = windowToOutput(ofVec2f(x, y));
		ndiPosition.x = outputPos.x - ndiDragOffset.x;
		ndiPosition.y = outputPos.y - ndiDragOffset.y;

		const float maxX = outputWidth - ndiSize.x;
		const float maxY = outputHeight - ndiSize.y;
		ndiPosition.x = ofClamp(ndiPosition.x, 0.0f, maxX);
		ndiPosition.y = ofClamp(ndiPosition.y, 0.0f, maxY);
	}

	if (draggingFade) {
		updateSelectedFoamFade(static_cast<float>(x));
	}

	if (draggingFoamBounce) {
		updateSelectedFoamBounce(static_cast<float>(x));
	}

	if (draggingMistSpeed) {
		const float t = ofClamp((x - mistSpeedRect.x) / mistSpeedRect.width, 0.0f, 1.0f);
		mistSpeed = ofLerp(0.2f, 2.0f, t);
	}

	if (draggingNdiFade) {
		const float t = ofClamp((x - ndiFadeRect.x) / ndiFadeRect.width, 0.0f, 1.0f);
		ndiFade = t;
	}

	if (draggingCycleDuration) {
		const float t = ofClamp((x - cycleDurationRect.x) / cycleDurationRect.width, 0.0f, 1.0f);
		cycleDuration = ofLerp(10.0f, 600.0f, t);
	}

	if (draggingCycleWindow) {
		const float t = ofClamp((x - cycleWindowRect.x) / cycleWindowRect.width, 0.0f, 1.0f);
		cycleTriggerWidth = ofLerp(0.0f, 0.5f, t);
	}

	if (draggingParticleSlider) {
		updateSelectedParticleSlider(static_cast<float>(x));
	}

	if (draggingEmitter && selectedParticleIndex >= 0 && selectedParticleIndex < static_cast<int>(particleSystems.size())) {
		const ofVec2f outputPos = windowToOutput(ofVec2f(x, y));
		ParticleSystem &system = particleSystems[selectedParticleIndex];
		if (system.locked) {
			return;
		}
		system.emitterRect.x = outputPos.x - emitterDragOffset.x;
		system.emitterRect.y = outputPos.y - emitterDragOffset.y;
		system.emitterRect.x = ofClamp(system.emitterRect.x, -system.emitterRect.width, static_cast<float>(outputWidth));
		system.emitterRect.y = ofClamp(system.emitterRect.y, 0.0f, outputHeight - system.emitterRect.height);
	}
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
	if (showGui) {
		const ofRectangle panelRect = gui.getShape();
		if (panelRect.inside(x, y)) {
			return;
		}
	}

	if (showGui) {
		if (uiLockRect.inside(x, y)) {
			configLocked = !configLocked;
			return;
		}
		if (configLocked) {
			return;
		}
		if (dropdownRect.inside(x, y)) {
			dropdownOpen = !dropdownOpen;
			return;
		}

		if (dropdownOpen) {
			const int itemCount = static_cast<int>(ndiSenders.size());
			for (int i = 0; i < itemCount; ++i) {
				ofRectangle itemRect(
					dropdownRect.x,
					dropdownRect.y + dropdownRect.height + i * dropdownItemHeight,
					dropdownRect.width,
					dropdownItemHeight);
				if (itemRect.inside(x, y)) {
					selectSenderIndex(i);
					dropdownOpen = false;
					return;
				}
			}
			dropdownOpen = false;
		}

		if (ndiEnableRect.inside(x, y)) {
			ndiEnabled = !ndiEnabled;
			if (!ndiEnabled && selectedLayer == LayerSelection::NDI) {
				selectedLayer = LayerSelection::None;
			}
			return;
		}

		if (ndiFadeRect.inside(x, y)) {
			draggingNdiFade = true;
			const float t = ofClamp((x - ndiFadeRect.x) / ndiFadeRect.width, 0.0f, 1.0f);
			ndiFade = t;
			return;
		}

		if (ndiTestRect.inside(x, y)) {
			showNdiTestPattern = !showNdiTestPattern;
			return;
		}

		if (foamEnableRect.inside(x, y)) {
			foamGroupEnabled = !foamGroupEnabled;
			if (!foamGroupEnabled && selectedLayer == LayerSelection::Foam) {
				selectedLayer = LayerSelection::None;
				selectedFoamIndex = -1;
			}
			return;
		}

		if (foamMistRect.inside(x, y)) {
			foamUseMist = !foamUseMist;
			return;
		}

		if (addFoamRect.inside(x, y)) {
			createFoamLayer();
			return;
		}

		if (deleteFoamRect.inside(x, y)) {
			removeSelectedFoamLayer();
			return;
		}

		if (fadeSliderRect.inside(x, y)) {
			draggingFade = true;
			updateSelectedFoamFade(static_cast<float>(x));
			return;
		}

		if (foamBounceRect.inside(x, y)) {
			draggingFoamBounce = true;
			updateSelectedFoamBounce(static_cast<float>(x));
			return;
		}

		if (mistSpeedRect.inside(x, y)) {
			draggingMistSpeed = true;
			const float t = ofClamp((x - mistSpeedRect.x) / mistSpeedRect.width, 0.0f, 1.0f);
			mistSpeed = ofLerp(0.2f, 2.0f, t);
			return;
		}

		if (particleEnableRect.inside(x, y)) {
			particleGroupEnabled = !particleGroupEnabled;
			if (!particleGroupEnabled && selectedLayer == LayerSelection::Particles) {
				selectedLayer = LayerSelection::None;
				selectedParticleIndex = -1;
			}
			return;
		}

		if (particleSpawnRect.inside(x, y)) {
			particleSpawnEnabled = !particleSpawnEnabled;
			if (!particleSpawnEnabled) {
				for (auto &system : particleSystems) {
					system.spawnAccumulator = 0.0f;
				}
			}
			return;
		}

		if (addParticleRect.inside(x, y)) {
			createParticleSystem();
			return;
		}

		if (deleteParticleRect.inside(x, y)) {
			removeSelectedParticleSystem();
			return;
		}

		if (particleSizeRect.inside(x, y)) {
			activeParticleSlider = 0;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleSpeedRect.inside(x, y)) {
			activeParticleSlider = 1;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleBounceRect.inside(x, y)) {
			activeParticleSlider = 2;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleAmountRect.inside(x, y)) {
			activeParticleSlider = 3;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleLifeRect.inside(x, y)) {
			activeParticleSlider = 4;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleTrailRect.inside(x, y)) {
			activeParticleSlider = 5;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleFadeRect.inside(x, y)) {
			activeParticleSlider = 6;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleNoiseRect.inside(x, y)) {
			activeParticleSlider = 7;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (particleNoiseStartRect.inside(x, y)) {
			activeParticleSlider = 8;
			draggingParticleSlider = true;
			updateSelectedParticleSlider(static_cast<float>(x));
			return;
		}

		if (cyclePlayRect.inside(x, y)) {
			cyclePlaying = true;
			return;
		}

		if (cycleStopRect.inside(x, y)) {
			cyclePlaying = false;
			return;
		}

		if (cycleDurationRect.inside(x, y)) {
			draggingCycleDuration = true;
			const float t = ofClamp((x - cycleDurationRect.x) / cycleDurationRect.width, 0.0f, 1.0f);
			cycleDuration = ofLerp(10.0f, 600.0f, t);
			return;
		}

		if (cycleWindowRect.inside(x, y)) {
			draggingCycleWindow = true;
			const float t = ofClamp((x - cycleWindowRect.x) / cycleWindowRect.width, 0.0f, 1.0f);
			cycleTriggerWidth = ofLerp(0.0f, 0.5f, t);
			return;
		}

		if (oscEnableRect.inside(x, y)) {
			oscEnabled = !oscEnabled;
			return;
		}

		if (resetRect.inside(x, y)) {
			const float now = ofGetElapsedTimef();
			if (resetArmed && (now - resetArmedTime) <= 2.0f) {
			resetComposition();
			resetArmed = false;
			currentPresetIndex = 0;
			isPresetTransition = false;
			presetTransitionAlpha = 1.0f;
			presetTransitionPhase = 0.0f;
			presetTransitionLoaded = false;
			pendingPresetIndex = 0;
			pendingKeepParticles = false;
		} else {
			resetArmed = true;
			resetArmedTime = now;
		}
		return;
	}

	for (int i = 0; i < static_cast<int>(presetRects.size()); ++i) {
		if (presetRects[i].inside(x, y)) {
			const bool savePreset = ofGetKeyPressed(OF_KEY_SHIFT);
			const bool clearPreset = ofGetKeyPressed(OF_KEY_ALT);
			const std::string presetPath = getPresetPath(i + 1);
			if (clearPreset) {
				const std::string resolved = ofToDataPath(presetPath, true);
				if (ofFile::doesFileExist(resolved)) {
					ofFile::removeFile(resolved);
				}
				if (currentPresetIndex == i + 1) {
					currentPresetIndex = 0;
				}
			} else if (savePreset) {
				saveComposition(presetPath, false);
				currentPresetIndex = i + 1;
			} else {
				startPresetTransition(i + 1);
			}
			return;
		}
	}

	for (int i = 0; i < static_cast<int>(colorRects.size()); ++i) {
		if (colorRects[i].inside(x, y)) {
			for (auto &system : particleSystems) {
				system.trailColorIndex = i;
			}
			currentColorIndex = i;
			sendOscColor(i);
			return;
		}
	}

	if (betterFpsRect.inside(x, y)) {
		previewEnabled = !previewEnabled;
		return;
	}

	if (presetTransitionRect.inside(x, y)) {
		const float t = ofClamp((x - presetTransitionRect.x) / presetTransitionRect.width, 0.0f, 1.0f);
		presetTransitionDuration = ofLerp(0.0f, 6.0f, t);
		return;
	}
	}

	if (configLocked || compactMode) {
		return;
	}
	if (selectedLayer == LayerSelection::Foam && selectedFoamIndex >= 0 &&
		selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		const FoamLayer &layer = foamLayers[selectedFoamIndex];
		const ofRectangle windowRect = outputToWindowRect(ofRectangle(layer.position.x, layer.position.y, layer.size.x, layer.size.y));
		const ofRectangle closeRect(windowRect.x, windowRect.y, 14.0f, 14.0f);
		const ofRectangle lockRect(windowRect.x + windowRect.width - 14.0f, windowRect.y, 14.0f, 14.0f);
		if (lockRect.inside(x, y)) {
			foamLayers[selectedFoamIndex].locked = !foamLayers[selectedFoamIndex].locked;
			return;
		}
		if (closeRect.inside(x, y)) {
			foamLayers.erase(foamLayers.begin() + selectedFoamIndex);
			selectedFoamIndex = foamLayers.empty() ? -1 : ofClamp(selectedFoamIndex, 0, static_cast<int>(foamLayers.size()) - 1);
			return;
		}
	}

	if (selectedLayer == LayerSelection::Particles && selectedParticleIndex >= 0 &&
		selectedParticleIndex < static_cast<int>(particleSystems.size())) {
		const ofRectangle windowRect = outputToWindowRect(particleSystems[selectedParticleIndex].emitterRect);
		const ofRectangle closeRect(windowRect.x, windowRect.y, 14.0f, 14.0f);
		const ofRectangle lockRect(windowRect.x + windowRect.width - 14.0f, windowRect.y, 14.0f, 14.0f);
		if (lockRect.inside(x, y)) {
			particleSystems[selectedParticleIndex].locked = !particleSystems[selectedParticleIndex].locked;
			return;
		}
		if (closeRect.inside(x, y)) {
			particleSystems.erase(particleSystems.begin() + selectedParticleIndex);
			selectedParticleIndex = particleSystems.empty() ? -1 : ofClamp(selectedParticleIndex, 0, static_cast<int>(particleSystems.size()) - 1);
			return;
		}
	}

	if (outputFbo.isAllocated() && previewRect.inside(x, y)) {
		const ofVec2f outputPos = windowToOutput(ofVec2f(x, y));
		bool hitLayer = false;
		int hitIndex = -1;
		for (int i = static_cast<int>(foamLayers.size()) - 1; i >= 0; --i) {
			if (!foamLayers[i].enabled) {
				continue;
			}
			const ofRectangle outputRect(foamLayers[i].position.x, foamLayers[i].position.y,
				foamLayers[i].size.x, foamLayers[i].size.y);
			const ofRectangle windowRect = outputToWindowRect(outputRect);
			const ofRectangle lockRect(windowRect.x + windowRect.width - 14.0f, windowRect.y, 14.0f, 14.0f);
			if (lockRect.inside(x, y)) {
				foamLayers[i].locked = !foamLayers[i].locked;
				return;
			}
		}

		for (int i = static_cast<int>(particleSystems.size()) - 1; i >= 0; --i) {
			if (!particleSystems[i].enabled) {
				continue;
			}
			const ofRectangle windowRect = outputToWindowRect(particleSystems[i].emitterRect);
			const ofRectangle lockRect(windowRect.x + windowRect.width - 14.0f, windowRect.y, 14.0f, 14.0f);
			if (lockRect.inside(x, y)) {
				particleSystems[i].locked = !particleSystems[i].locked;
				return;
			}
		}
		if (hitTestFoamLayer(outputPos, hitIndex)) {
			selectedFoamIndex = hitIndex;
			selectedLayer = LayerSelection::Foam;
			draggingFoam = true;
			const FoamLayer &layer = foamLayers[selectedFoamIndex];
			if (layer.locked) {
				draggingFoam = false;
				return;
			}
			foamDragOffset.set(outputPos.x - layer.position.x, outputPos.y - layer.position.y);
			hitLayer = true;
			return;
		}

		if (!particleSystems.empty() && particleGroupEnabled) {
			for (int i = static_cast<int>(particleSystems.size()) - 1; i >= 0; --i) {
				if (particleSystems[i].enabled && particleSystems[i].emitterRect.inside(outputPos)) {
					if (particleSystems[i].locked) {
						hitLayer = true;
						return;
					}
					selectedParticleIndex = i;
					selectedLayer = LayerSelection::Particles;
					draggingEmitter = true;
					emitterDragOffset.set(outputPos.x - particleSystems[i].emitterRect.x,
						outputPos.y - particleSystems[i].emitterRect.y);
					hitLayer = true;
					return;
				}
			}
		}

		if (ndiEnabled && ndiTexture.isAllocated()) {
			const ofRectangle ndiRect = getNdiOutputRect();
			if (ndiRect.inside(outputPos)) {
				selectedLayer = LayerSelection::NDI;
				draggingNdi = true;
				ndiDragOffset.set(outputPos.x - ndiPosition.x, outputPos.y - ndiPosition.y);
				hitLayer = true;
			}
		}

		if (!hitLayer) {
			selectedLayer = LayerSelection::None;
			selectedFoamIndex = -1;
			selectedParticleIndex = -1;
		}
	} else {
		selectedLayer = LayerSelection::None;
		selectedFoamIndex = -1;
		selectedParticleIndex = -1;
	}
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
	draggingFoam = false;
	draggingNdi = false;
	draggingFade = false;
	draggingFoamBounce = false;
	draggingMistSpeed = false;
	draggingNdiFade = false;
	draggingParticleSlider = false;
	activeParticleSlider = -1;
	draggingEmitter = false;
	draggingCycleDuration = false;
	draggingCycleWindow = false;

}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){
	if (configLocked || compactMode) {
		return;
	}
	if (selectedLayer == LayerSelection::Foam && selectedFoamIndex >= 0 &&
		selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		FoamLayer &layer = foamLayers[selectedFoamIndex];
		const float scale = 1.0f + scrollY * 0.05f;
		const ofVec2f center = layer.position + layer.size * 0.5f;
		layer.size.x = ofClamp(layer.size.x * scale, 50.0f, static_cast<float>(outputWidth));
		layer.size.y = ofClamp(layer.size.y * scale, 50.0f, static_cast<float>(outputHeight));
		layer.position = center - layer.size * 0.5f;
		layer.position.x = ofClamp(layer.position.x, -layer.size.x, static_cast<float>(outputWidth));
		layer.position.y = ofClamp(layer.position.y, 0.0f, outputHeight - layer.size.y);
		const int fboW = std::max(1, static_cast<int>(layer.size.x * foamRenderScale));
		const int fboH = std::max(1, static_cast<int>(layer.size.y * foamRenderScale));
		layer.fbo.allocate(fboW, fboH, GL_RGBA);
		layer.fbo.begin();
		ofClear(0, 0, 0, 0);
		layer.fbo.end();
		return;
	}

	if (selectedLayer == LayerSelection::Particles && selectedParticleIndex >= 0 &&
		selectedParticleIndex < static_cast<int>(particleSystems.size())) {
		ParticleSystem &system = particleSystems[selectedParticleIndex];
		const float scale = 1.0f + scrollY * 0.05f;
		const ofVec2f center = system.emitterRect.getCenter();
		system.emitterRect.width = ofClamp(system.emitterRect.width * scale, 20.0f, static_cast<float>(outputWidth));
		system.emitterRect.height = ofClamp(system.emitterRect.height * scale, 20.0f, static_cast<float>(outputHeight));
		system.emitterRect.setFromCenter(center, system.emitterRect.width, system.emitterRect.height);
		system.emitterRect.x = ofClamp(system.emitterRect.x, -system.emitterRect.width, static_cast<float>(outputWidth));
		system.emitterRect.y = ofClamp(system.emitterRect.y, 0.0f, outputHeight - system.emitterRect.height);
		return;
	}
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	updatePreviewRect();

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

//--------------------------------------------------------------
void ofApp::refreshNdiSenders(){
	lastSenderScanTime = ofGetElapsedTimef();

	const int senderCount = ndiReceiver.FindSenders();
	ndiAvailableSenderCount = std::max(0, senderCount);
	ndiSenders = ndiReceiver.GetSenderList();
	ndiSenders.insert(ndiSenders.begin(), "<none>");

	int desiredIndex = -1;
	if (!ndiDesiredSenderName.empty()) {
		for (int i = 1; i < static_cast<int>(ndiSenders.size()); ++i) {
			if (ndiSenders[i] == ndiDesiredSenderName) {
				desiredIndex = i;
				break;
			}
		}
		if (desiredIndex < 0) {
			ndiSenders.push_back(ndiDesiredSenderName);
			desiredIndex = static_cast<int>(ndiSenders.size()) - 1;
		}
	}

	const int maxIndex = static_cast<int>(ndiSenders.size()) - 1;
	if (!ndiDesiredSenderName.empty() && desiredIndex >= 0) {
		selectedSenderIndex = desiredIndex;
	} else if (selectedSenderIndex < 0 || selectedSenderIndex > maxIndex) {
		selectedSenderIndex = (senderCount > 0) ? 1 : 0;
	}

	const bool senderAvailable = selectedSenderIndex > 0 && selectedSenderIndex <= ndiAvailableSenderCount;
	if (!senderAvailable) {
		ndiReceiver.ReleaseReceiver();
		return;
	}

	ndiReceiver.SetSenderName(ndiSenders[selectedSenderIndex]);
	if (!ndiReceiver.ReceiverCreated()) {
		ndiReceiver.CreateReceiver(-1);
	}
}

//--------------------------------------------------------------
void ofApp::selectSenderIndex(int index){
	if (ndiSenders.empty()) {
		return;
	}

	const int maxIndex = static_cast<int>(ndiSenders.size()) - 1;
	const int clampedIndex = ofClamp(index, 0, maxIndex);
	if (clampedIndex == selectedSenderIndex) {
		return;
	}

	selectedSenderIndex = clampedIndex;

	if (selectedSenderIndex == 0) {
		ndiReceiver.ReleaseReceiver();
		ndiDesiredSenderName.clear();
		return;
	}

	if (ndiSenders.empty() || selectedSenderIndex >= static_cast<int>(ndiSenders.size())) {
		selectedSenderIndex = -1;
		ndiReceiver.ReleaseReceiver();
		return;
	}
	ndiDesiredSenderName = ndiSenders[selectedSenderIndex];
	if (selectedSenderIndex > ndiAvailableSenderCount) {
		ndiReceiver.ReleaseReceiver();
		return;
	}
	ndiReceiver.ReleaseReceiver();
	ndiReceiver.SetSenderName(ndiSenders[selectedSenderIndex]);
	ndiReceiver.CreateReceiver(-1);
}

//--------------------------------------------------------------
void ofApp::drawNdiDropdown(){
	auto formatNdiName = [](const std::string &name) {
		auto trim = [](std::string value) {
			const auto start = value.find_first_not_of(" \t");
			if (start == std::string::npos) {
				return std::string();
			}
			const auto end = value.find_last_not_of(" \t");
			return value.substr(start, end - start + 1);
		};

		std::string trimmed = name;

		const auto parenOpen = trimmed.find(" (");
		const auto parenClose = trimmed.find(")");
		if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen + 2) {
			const std::string inside = trimmed.substr(parenOpen + 2, parenClose - (parenOpen + 2));
			if (!inside.empty()) {
				return trim(inside);
			}
		}

		const auto colonPos = trimmed.find(":");
		if (colonPos != std::string::npos && colonPos + 1 < trimmed.size()) {
			return trim(trimmed.substr(colonPos + 1));
		}

		const auto atPos = trimmed.find(" @");
		if (atPos != std::string::npos) {
			return trim(trimmed.substr(0, atPos));
		}

		return trim(trimmed);
	};

	const ofRectangle panelRect = gui.getShape();
	const float dropdownX = panelRect.x;
	const float dropdownY = panelRect.y + panelRect.height + 28.0f;
	const float dropdownW = panelRect.width;
	dropdownRect.set(dropdownX, dropdownY, dropdownW, dropdownItemHeight);
	const float buttonSize = 24.0f;
	const float sliderHeight = 10.0f;
	const float sliderWidth = panelRect.width - (buttonSize * 2.0f + 16.0f);

	std::string currentName = "<none>";
	if (selectedSenderIndex >= 0 && selectedSenderIndex < static_cast<int>(ndiSenders.size())) {
		currentName = formatNdiName(ndiSenders[selectedSenderIndex]);
	}

	ofPushStyle();
	ofSetColor(200);
	ofDrawBitmapString("NDI INPUT", dropdownRect.x, dropdownRect.y - 10.0f);
	ndiEnableRect.set(dropdownRect.x, dropdownRect.y + dropdownRect.height + 8.0f,
		buttonSize, buttonSize);
	const float fadeX = dropdownRect.x + buttonSize * 2.0f + 16.0f;
	const float fadeY = ndiEnableRect.y + (buttonSize - sliderHeight) * 0.5f;
	ndiFadeRect.set(fadeX, fadeY, sliderWidth, sliderHeight);
	ndiTestRect.set(ndiEnableRect.x + buttonSize + 6.0f, ndiEnableRect.y, buttonSize, buttonSize);
	ofSetColor(40);
	ofDrawRectangle(ndiEnableRect);
	ofDrawRectangle(ndiTestRect);
	ofSetColor(255);
	if (ndiEnabled) {
		ofDrawBitmapString("X", ndiEnableRect.getCenter().x - 3.0f, ndiEnableRect.getCenter().y + 5.0f);
	}
	if (showNdiTestPattern) {
		ofDrawBitmapString("T", ndiTestRect.getCenter().x - 3.0f, ndiTestRect.getCenter().y + 5.0f);
	}
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(ndiEnableRect);
	ofDrawRectangle(ndiTestRect);
	ofFill();
	ofSetColor(60);
	ofDrawRectangle(ndiFadeRect);
	ofSetColor(180);
	ofDrawRectangle(ndiFadeRect.x, ndiFadeRect.y, ndiFadeRect.width * ofClamp(ndiFade, 0.0f, 1.0f), ndiFadeRect.height);
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(ndiFadeRect);
	ofFill();
	const bool senderSelected = selectedSenderIndex > 0;
	const bool senderAvailable = senderSelected && selectedSenderIndex <= ndiAvailableSenderCount;
	const bool senderConnected = senderAvailable && ndiEnabled && ndiReceiver.ReceiverConnected();
	if (senderSelected && senderConnected) {
		ofSetColor(20, 90, 45);
	} else if (senderSelected && !senderConnected) {
		ofSetColor(110, 30, 30);
	} else {
		ofSetColor(30);
	}
	ofDrawRectangle(dropdownRect);
	ofSetColor(255);
	ofDrawBitmapString(currentName,
		dropdownRect.x + 6.0f, dropdownRect.y + dropdownItemHeight - 6.0f);
	ofNoFill();
	ofSetColor(120);
	ofDrawRectangle(dropdownRect);
	ofFill();

	const int mx = ofGetMouseX();
	const int my = ofGetMouseY();
	if (draggingNdiFade || ndiFadeRect.inside(mx, my)) {
		drawSliderLabel("FADE", ndiFadeRect);
	} else if (ndiTestRect.inside(mx, my)) {
		drawSliderLabel("NDI TEST", ndiTestRect);
	}

	if (dropdownOpen) {
		const int itemCount = static_cast<int>(ndiSenders.size());
		for (int i = 0; i < itemCount; ++i) {
			ofRectangle itemRect(
				dropdownRect.x,
				dropdownRect.y + dropdownRect.height + i * dropdownItemHeight,
				dropdownRect.width,
				dropdownItemHeight);
			ofSetColor(i == selectedSenderIndex ? 70 : 45);
			ofDrawRectangle(itemRect);
			ofSetColor(255);
			ofDrawBitmapString(formatNdiName(ndiSenders[i]),
				itemRect.x + 6.0f, itemRect.y + dropdownItemHeight - 6.0f);
			ofNoFill();
			ofSetColor(90);
			ofDrawRectangle(itemRect);
			ofFill();
		}
	}

	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::drawFoamControls(){
	const ofRectangle panelRect = gui.getShape();
	const float controlsY = ndiEnableRect.y + ndiEnableRect.height + 34.0f;
	const float controlsX = panelRect.x;
	const float buttonSize = 24.0f;
	const float sliderWidth = panelRect.width - (buttonSize * 2.0f + 16.0f);
	const float sliderHeight = 10.0f;

	addFoamRect.set(controlsX, controlsY, buttonSize, buttonSize);
	deleteFoamRect.set(addFoamRect.x + buttonSize + 8.0f, controlsY, buttonSize, buttonSize);
	foamEnableRect.set(controlsX, controlsY + buttonSize + 8.0f, buttonSize, buttonSize);
	foamMistRect.set(foamEnableRect.x + buttonSize + 6.0f, foamEnableRect.y, buttonSize, buttonSize);
	const float fadeX = deleteFoamRect.x + buttonSize + 8.0f;
	const float fadeW = panelRect.width - (fadeX - panelRect.x);
	fadeSliderRect.set(fadeX, controlsY, fadeW, sliderHeight);
	foamBounceRect.set(fadeX, fadeSliderRect.y + 12.0f, fadeW, sliderHeight);
	mistSpeedRect.set(fadeX, foamBounceRect.y + 12.0f, fadeW, sliderHeight);

	ofPushStyle();
	ofSetColor(200);
ofDrawBitmapString("FOAM", controlsX, controlsY - 7.0f);

	ofSetColor(40);
	ofDrawRectangle(addFoamRect);
	ofDrawRectangle(deleteFoamRect);
	ofDrawRectangle(foamEnableRect);
	ofDrawRectangle(foamMistRect);

	ofSetColor(255);
	ofDrawBitmapString("+", addFoamRect.getCenter().x - 3.0f, addFoamRect.getCenter().y + 5.0f);
	ofDrawBitmapString("-", deleteFoamRect.getCenter().x - 3.0f, deleteFoamRect.getCenter().y + 5.0f);
	if (foamGroupEnabled) {
		ofDrawBitmapString("X", foamEnableRect.getCenter().x - 3.0f, foamEnableRect.getCenter().y + 5.0f);
	}
	if (foamUseMist) {
		ofDrawBitmapString("M", foamMistRect.getCenter().x - 3.0f, foamMistRect.getCenter().y + 5.0f);
	}

	float fadeValue = 0.0f;
	float bounceValue = 0.0f;
	if (selectedFoamIndex >= 0 && selectedFoamIndex < static_cast<int>(foamLayers.size())) {
		fadeValue = foamLayers[selectedFoamIndex].fade;
		bounceValue = ofClamp(foamLayers[selectedFoamIndex].bounceOffset, 0.0f, 1.0f);
	}

	ofSetColor(60);
	ofDrawRectangle(fadeSliderRect);
	ofDrawRectangle(foamBounceRect);
	const float filledWidth = fadeSliderRect.width * fadeValue;
	ofSetColor(180);
	ofDrawRectangle(fadeSliderRect.x, fadeSliderRect.y, filledWidth, fadeSliderRect.height);
	ofDrawRectangle(foamBounceRect.x, foamBounceRect.y, foamBounceRect.width * bounceValue, foamBounceRect.height);

	const float mistSpeedValue = ofClamp(ofMap(mistSpeed, 0.2f, 2.0f, 0.0f, 1.0f, true), 0.0f, 1.0f);
	ofSetColor(60);
	ofDrawRectangle(mistSpeedRect);
	ofSetColor(180);
	ofDrawRectangle(mistSpeedRect.x, mistSpeedRect.y, mistSpeedRect.width * mistSpeedValue, mistSpeedRect.height);

	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(fadeSliderRect);
	ofDrawRectangle(foamBounceRect);
	ofDrawRectangle(mistSpeedRect);
	ofFill();

	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(addFoamRect);
	ofDrawRectangle(deleteFoamRect);
	ofDrawRectangle(foamEnableRect);
	ofDrawRectangle(foamMistRect);
	ofFill();

	const int mx = ofGetMouseX();
	const int my = ofGetMouseY();
	if (draggingFade || fadeSliderRect.inside(mx, my)) {
		drawSliderLabel("FADE", fadeSliderRect);
	} else if (draggingFoamBounce || foamBounceRect.inside(mx, my)) {
		drawSliderLabel("BOUNCE WIDTH", foamBounceRect);
	} else if (draggingMistSpeed || mistSpeedRect.inside(mx, my)) {
		drawSliderLabel("MIST SPEED", mistSpeedRect);
	} else if (foamMistRect.inside(mx, my)) {
		ofSetColor(200);
		const float labelX = foamMistRect.x + foamMistRect.width + 8.0f;
		const float labelY = foamMistRect.y + foamMistRect.height - 8.0f;
		ofDrawBitmapString("MIST", labelX, labelY);
	}

	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::drawParticleControls(){
	const ofRectangle panelRect = gui.getShape();
	const float controlsY = foamEnableRect.y + foamEnableRect.height + 34.0f;
	const float controlsX = panelRect.x;
	const float buttonSize = 24.0f;
	const float sliderWidth = panelRect.width - (buttonSize * 2.0f + 16.0f);
	const float sliderHeight = 10.0f;

addParticleRect.set(controlsX, controlsY, buttonSize, buttonSize);
deleteParticleRect.set(addParticleRect.x + buttonSize + 8.0f, controlsY, buttonSize, buttonSize);
	particleEnableRect.set(controlsX, controlsY + buttonSize + 8.0f, buttonSize, buttonSize);
	particleSpawnRect.set(particleEnableRect.x + buttonSize + 6.0f, particleEnableRect.y, buttonSize, buttonSize);

	const float sliderX = deleteParticleRect.x + buttonSize + 8.0f;
	particleSizeRect.set(sliderX, controlsY + 2.0f, sliderWidth, sliderHeight);
	particleSpeedRect.set(sliderX, controlsY + 14.0f, sliderWidth, sliderHeight);
	particleBounceRect.set(sliderX, controlsY + 26.0f, sliderWidth, sliderHeight);
	particleAmountRect.set(sliderX, controlsY + 38.0f, sliderWidth, sliderHeight);
	particleLifeRect.set(sliderX, controlsY + 50.0f, sliderWidth, sliderHeight);
	particleTrailRect.set(sliderX, controlsY + 62.0f, sliderWidth, sliderHeight);
	particleFadeRect.set(sliderX, controlsY + 74.0f, sliderWidth, sliderHeight);
	particleNoiseRect.set(sliderX, controlsY + 86.0f, sliderWidth, sliderHeight);
	particleNoiseStartRect.set(sliderX, controlsY + 98.0f, sliderWidth, sliderHeight);

	float sizeValue = 0.0f;
	float speedValue = 0.0f;
	float bounceValue = 0.0f;
	float amountValue = 0.0f;
	float lifeValue = 0.0f;
	float trailValue = 0.0f;
	float fadeValue = 0.0f;
	float noiseValue = 0.0f;
	float noiseStartValue = 0.0f;
	if (selectedParticleIndex >= 0 && selectedParticleIndex < static_cast<int>(particleSystems.size())) {
		const ParticleSystem &system = particleSystems[selectedParticleIndex];
		sizeValue = ofMap(system.size, 1.0f, 12.0f, 0.0f, 1.0f, true);
		speedValue = ofMap(system.speed, 10.0f, 260.0f, 0.0f, 1.0f, true);
		bounceValue = ofMap(system.bounce, 0.1f, 0.95f, 0.0f, 1.0f, true);
		amountValue = ofMap(system.maxParticles, 240.0f, 1500.0f, 0.0f, 1.0f, true);
		lifeValue = ofMap(system.lifeSpanMax - system.lifeSpanMin, 0.0f, 20.0f, 0.0f, 1.0f, true);
		trailValue = ofClamp(system.trail / 3.0f, 0.0f, 1.0f);
		fadeValue = ofClamp(system.fade, 0.0f, 1.0f);
		noiseValue = ofClamp(system.noise / 200.0f, 0.0f, 1.0f);
		noiseStartValue = ofClamp(system.noiseStart, 0.0f, 1.0f);
	}

	ofPushStyle();
	ofSetColor(200);
ofDrawBitmapString("PARTICLES", controlsX, controlsY - 7.0f);

ofSetColor(40);
ofDrawRectangle(addParticleRect);
ofDrawRectangle(deleteParticleRect);
ofDrawRectangle(particleEnableRect);
if (particleSpawnEnabled) {
	ofSetColor(50, 120, 70);
} else {
	ofSetColor(140, 50, 50);
}
ofDrawRectangle(particleSpawnRect);

ofSetColor(255);
ofDrawBitmapString("+", addParticleRect.getCenter().x - 3.0f, addParticleRect.getCenter().y + 5.0f);
ofDrawBitmapString("-", deleteParticleRect.getCenter().x - 3.0f, deleteParticleRect.getCenter().y + 5.0f);
	if (particleGroupEnabled) {
		ofDrawBitmapString("X", particleEnableRect.getCenter().x - 3.0f, particleEnableRect.getCenter().y + 5.0f);
	}
	if (particleSpawnEnabled) {
		ofDrawBitmapString("S", particleSpawnRect.getCenter().x - 3.0f, particleSpawnRect.getCenter().y + 5.0f);
	} else {
		ofDrawBitmapString("S", particleSpawnRect.getCenter().x - 3.0f, particleSpawnRect.getCenter().y + 5.0f);
	}

	ofSetColor(60);
	ofDrawRectangle(particleSizeRect);
	ofDrawRectangle(particleSpeedRect);
	ofDrawRectangle(particleBounceRect);
	ofDrawRectangle(particleAmountRect);
	ofDrawRectangle(particleLifeRect);
	ofDrawRectangle(particleTrailRect);
	ofDrawRectangle(particleFadeRect);
	ofDrawRectangle(particleNoiseRect);
	ofDrawRectangle(particleNoiseStartRect);
	ofSetColor(180);
	ofDrawRectangle(particleSizeRect.x, particleSizeRect.y, particleSizeRect.width * sizeValue, particleSizeRect.height);
	ofDrawRectangle(particleSpeedRect.x, particleSpeedRect.y, particleSpeedRect.width * speedValue, particleSpeedRect.height);
	ofDrawRectangle(particleBounceRect.x, particleBounceRect.y, particleBounceRect.width * bounceValue, particleBounceRect.height);
	ofDrawRectangle(particleAmountRect.x, particleAmountRect.y, particleAmountRect.width * amountValue, particleAmountRect.height);
	ofDrawRectangle(particleLifeRect.x, particleLifeRect.y, particleLifeRect.width * lifeValue, particleLifeRect.height);
	ofDrawRectangle(particleTrailRect.x, particleTrailRect.y, particleTrailRect.width * trailValue, particleTrailRect.height);
	ofDrawRectangle(particleFadeRect.x, particleFadeRect.y, particleFadeRect.width * fadeValue, particleFadeRect.height);
	ofDrawRectangle(particleNoiseRect.x, particleNoiseRect.y, particleNoiseRect.width * noiseValue, particleNoiseRect.height);
	ofDrawRectangle(particleNoiseStartRect.x, particleNoiseStartRect.y, particleNoiseStartRect.width * noiseStartValue, particleNoiseStartRect.height);

	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(addParticleRect);
	ofDrawRectangle(deleteParticleRect);
	ofDrawRectangle(particleEnableRect);
	ofDrawRectangle(particleSpawnRect);
	ofDrawRectangle(particleSizeRect);
	ofDrawRectangle(particleSpeedRect);
	ofDrawRectangle(particleBounceRect);
	ofDrawRectangle(particleAmountRect);
	ofDrawRectangle(particleLifeRect);
	ofDrawRectangle(particleTrailRect);
	ofDrawRectangle(particleFadeRect);
	ofDrawRectangle(particleNoiseRect);
	ofDrawRectangle(particleNoiseStartRect);
	ofFill();

	const int mx = ofGetMouseX();
	const int my = ofGetMouseY();

	const float presetTitleY = particleNoiseStartRect.y + particleNoiseStartRect.height + 24.0f;
	ofSetColor(200);
	ofDrawBitmapString("PRESETS", controlsX, presetTitleY);
	const float presetY = presetTitleY + 10.0f;
	const float presetSize = 22.0f;
	const float presetGap = 6.0f;
	for (int i = 0; i < static_cast<int>(presetRects.size()); ++i) {
		presetRects[i].set(controlsX + i * (presetSize + presetGap), presetY, presetSize, presetSize);
		const std::string presetPath = getPresetPath(i + 1);
		const bool hasPreset = ofFile::doesFileExist(ofToDataPath(presetPath, true));
		const bool isHover = presetRects[i].inside(mx, my);
		const bool isActive = currentPresetIndex == i + 1;
		if (isActive) {
			ofSetColor(70, 120, 90);
		} else if (hasPreset) {
			ofSetColor(60, 70, 90);
		} else {
			ofSetColor(40);
		}
		if (isHover && !isActive) {
			ofSetColor(90, 110, 140);
		}
		ofDrawRectangle(presetRects[i]);
		ofSetColor(220);
		ofDrawBitmapString(ofToString(i + 1), presetRects[i].getCenter().x - 3.0f, presetRects[i].getCenter().y + 5.0f);
		ofNoFill();
		ofSetColor(110);
		ofDrawRectangle(presetRects[i]);
		ofFill();
	}

    ofSetColor(200);
    const float presetHintY = presetY + presetSize + 22.0f;
    ofDrawBitmapString("SHIFT=SAVE  ALT=CLEAR", controlsX, presetHintY);
	if (currentPresetIndex > 0) {
		ofSetColor(160);
		ofDrawBitmapString("ACTIVE: " + ofToString(currentPresetIndex), controlsX, presetHintY + 14.0f);
	}

    const float transitionTitleY = presetHintY + 34.0f;
    ofSetColor(200);
    ofDrawBitmapString("TRANSITION", controlsX, transitionTitleY);
    presetTransitionRect.set(controlsX, transitionTitleY + 10.0f, panelRect.width, 10.0f);
    ofSetColor(60);
    ofDrawRectangle(presetTransitionRect);
    ofSetColor(180);
    ofDrawRectangle(presetTransitionRect.x, presetTransitionRect.y,
        presetTransitionRect.width * ofClamp(presetTransitionDuration / 6.0f, 0.0f, 1.0f),
        presetTransitionRect.height);
    ofNoFill();
    ofSetColor(110);
    ofDrawRectangle(presetTransitionRect);
    ofFill();
    ofSetColor(200);
    ofDrawBitmapString(ofToString(presetTransitionDuration, 1) + "s",
        presetTransitionRect.x + presetTransitionRect.width - 26.0f,
        presetTransitionRect.y + presetTransitionRect.height + 12.0f);

    const float resetY = presetTransitionRect.y + presetTransitionRect.height + 18.0f;
    const float resetWidth = panelRect.width * 0.7f;
    resetRect.set(controlsX, resetY, resetWidth, 24.0f);
    ofSetColor(40);
    ofDrawRectangle(resetRect);
    ofSetColor(255);
    if (resetArmed) {
        ofSetColor(180, 30, 30);
        ofDrawBitmapString("CONFIRM", resetRect.x + 6.0f, resetRect.getCenter().y + 5.0f);
    } else {
        ofSetColor(255);
        ofDrawBitmapString("RESET COMP", resetRect.x + 6.0f, resetRect.getCenter().y + 5.0f);
    }
    ofNoFill();
    ofSetColor(110);
    ofDrawRectangle(resetRect);
    ofFill();

    const float colorsTitleY = resetRect.y + resetRect.height + 24.0f;
    ofSetColor(200);
    ofDrawBitmapString("COLORS", controlsX, colorsTitleY);
    const float colorsY = colorsTitleY + 12.0f;
    for (int i = 0; i < static_cast<int>(colorRects.size()); ++i) {
        colorRects[i].set(controlsX + i * (presetSize + presetGap), colorsY, presetSize, presetSize);
        const bool isActive = currentColorIndex == i;
        const bool isHover = colorRects[i].inside(mx, my);
        if (isActive) {
            ofSetColor(70, 120, 90);
        } else {
            ofSetColor(40);
        }
        if (i == 1) {
            ofSetColor(120, 40, 40);
        } else if (i == 2) {
            ofSetColor(140, 110, 40);
        } else if (i == 3) {
            ofSetColor(50, 90, 140);
        } else if (i == 4) {
            ofSetColor(60, 120, 70);
        }
        if (isHover && !isActive) {
            ofColor hoverColor = ofGetStyle().color;
            hoverColor.r = std::min(255, hoverColor.r + 30);
            hoverColor.g = std::min(255, hoverColor.g + 30);
            hoverColor.b = std::min(255, hoverColor.b + 30);
            ofSetColor(hoverColor);
        }
        ofDrawRectangle(colorRects[i]);
		if (isActive) {
			ofSetColor(230);
			ofDrawCircle(colorRects[i].getCenter(), 3.0f);
		}
        ofNoFill();
        ofSetColor(isHover ? ofColor(220) : (isActive ? ofColor(200) : ofColor(110)));
        ofDrawRectangle(colorRects[i]);
        ofFill();
    }

	const float cycleTitleY = colorsY + presetSize + 34.0f;
	ofSetColor(200);
	ofDrawBitmapString("SEQUENCE", controlsX, cycleTitleY);
	const float cycleRowY = cycleTitleY + 12.0f;
	cyclePlayRect.set(controlsX, cycleRowY, presetSize, presetSize);
	cycleStopRect.set(cyclePlayRect.x + presetSize + 6.0f, cycleRowY, presetSize, presetSize);
	const float cycleBarX = cycleStopRect.x + presetSize + 16.0f;
	const float cycleBarW = panelRect.width - (cycleBarX - panelRect.x);
	cycleProgressRect.set(cycleBarX, cycleRowY + 6.0f, cycleBarW, 10.0f);

	const bool playHover = cyclePlayRect.inside(mx, my);
	const bool stopHover = cycleStopRect.inside(mx, my);
	if (cyclePlaying) {
		ofSetColor(50, 120, 70);
	} else if (playHover) {
		ofSetColor(80);
	} else {
		ofSetColor(40);
	}
	ofDrawRectangle(cyclePlayRect);
	if (cyclePlaying) {
		ofSetColor(70, 30, 30);
	} else if (stopHover) {
		ofSetColor(80);
	} else {
		ofSetColor(40);
	}
	ofDrawRectangle(cycleStopRect);
	ofSetColor(255);
	ofDrawBitmapString(">", cyclePlayRect.getCenter().x - 3.0f, cyclePlayRect.getCenter().y + 5.0f);
	ofDrawBitmapString("[]", cycleStopRect.getCenter().x - 5.0f, cycleStopRect.getCenter().y + 5.0f);
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(cyclePlayRect);
	ofDrawRectangle(cycleStopRect);
	ofFill();

	ofSetColor(60);
	ofDrawRectangle(cycleProgressRect);
	ofSetColor(180);
	ofDrawRectangle(cycleProgressRect.x, cycleProgressRect.y,
		cycleProgressRect.width * ofClamp(cyclePhase, 0.0f, 1.0f),
		cycleProgressRect.height);
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(cycleProgressRect);
	ofFill();

	const float cycleDurationY = cycleProgressRect.y + cycleProgressRect.height + 18.0f;
	cycleDurationRect.set(controlsX, cycleDurationY, panelRect.width, 10.0f);
	ofSetColor(60);
	ofDrawRectangle(cycleDurationRect);
	ofSetColor(180);
	ofDrawRectangle(cycleDurationRect.x, cycleDurationRect.y,
		cycleDurationRect.width * ofClamp((cycleDuration - 10.0f) / (600.0f - 10.0f), 0.0f, 1.0f),
		cycleDurationRect.height);
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(cycleDurationRect);
	ofFill();
	ofSetColor(200);
	ofDrawBitmapString("CYCLE " + ofToString(cycleDuration, 0) + "s",
		cycleDurationRect.x + cycleDurationRect.width - 70.0f,
		cycleDurationRect.y + cycleDurationRect.height + 12.0f);

	const float cycleWindowY = cycleDurationRect.y + cycleDurationRect.height + 18.0f;
	cycleWindowRect.set(controlsX, cycleWindowY, panelRect.width, 10.0f);
	ofSetColor(60);
	ofDrawRectangle(cycleWindowRect);
	ofSetColor(180);
	ofDrawRectangle(cycleWindowRect.x, cycleWindowRect.y,
		cycleWindowRect.width * ofClamp(cycleTriggerWidth / 0.5f, 0.0f, 1.0f),
		cycleWindowRect.height);
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(cycleWindowRect);
	ofFill();
	ofSetColor(200);
	ofDrawBitmapString("WINDOW " + ofToString(cycleTriggerWidth * 100.0f, 0) + "%",
		cycleWindowRect.x + cycleWindowRect.width - 70.0f,
		cycleWindowRect.y + cycleWindowRect.height + 12.0f);

	const float oscTitleY = cycleWindowRect.y + cycleWindowRect.height + 34.0f;
	ofSetColor(200);
	ofDrawBitmapString("OSC OUT", controlsX, oscTitleY);
	const float oscRowY = oscTitleY + 12.0f;
	oscEnableRect.set(controlsX, oscRowY, presetSize, presetSize);
	ofSetColor(40);
	ofDrawRectangle(oscEnableRect);
	ofSetColor(255);
	if (oscEnabled) {
		ofDrawBitmapString("X", oscEnableRect.getCenter().x - 3.0f, oscEnableRect.getCenter().y + 5.0f);
	}
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(oscEnableRect);
	ofFill();
	ofSetColor(200);
	ofDrawBitmapString(oscHost + ":" + ofToString(oscPort),
		oscEnableRect.x + oscEnableRect.width + 8.0f,
		oscEnableRect.y + oscEnableRect.height - 6.0f);

	const float betterFpsTitleY = oscEnableRect.y + oscEnableRect.height + 34.0f;
	ofSetColor(200);
	ofDrawBitmapString("BETTER FPS", controlsX, betterFpsTitleY);
	const float betterFpsRowY = betterFpsTitleY + 12.0f;
	betterFpsRect.set(controlsX, betterFpsRowY, presetSize, presetSize);
	ofSetColor(40);
	ofDrawRectangle(betterFpsRect);
	ofSetColor(255);
	if (previewEnabled) {
		ofDrawBitmapString("X", betterFpsRect.getCenter().x - 3.0f, betterFpsRect.getCenter().y + 5.0f);
	}
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(betterFpsRect);
	ofFill();
	ofSetColor(200);
	const float lockX = betterFpsRect.x + presetSize + 6.0f;
	uiLockRect.set(lockX, betterFpsRect.y, presetSize, presetSize);
	ofSetColor(40);
	ofDrawRectangle(uiLockRect);
	if (configLocked) {
		ofSetColor(180, 60, 60);
		ofDrawBitmapString("L", uiLockRect.getCenter().x - 3.0f, uiLockRect.getCenter().y + 5.0f);
	} else {
		ofSetColor(255);
	}
	ofNoFill();
	ofSetColor(110);
	ofDrawRectangle(uiLockRect);
	ofFill();
	ofSetColor(200);
	ofDrawBitmapString("LOCK UI", uiLockRect.x + uiLockRect.width + 8.0f,
		uiLockRect.y + uiLockRect.height - 6.0f);

	// NDI test button removed from bottom; now lives next to NDI enable toggle.
    // Transition slider now sits between PRESETS and COLORS.
	if (draggingParticleSlider) {
		if (activeParticleSlider == 0) {
			drawSliderLabel("SIZE", particleSizeRect);
		} else if (activeParticleSlider == 1) {
			drawSliderLabel("SPEED", particleSpeedRect);
		} else if (activeParticleSlider == 2) {
			drawSliderLabel("BOUNCE", particleBounceRect);
		} else if (activeParticleSlider == 3) {
			drawSliderLabel("AMOUNT", particleAmountRect);
		} else if (activeParticleSlider == 4) {
			drawSliderLabel("LIFE", particleLifeRect);
		} else if (activeParticleSlider == 5) {
			drawSliderLabel("TRAIL", particleTrailRect);
		} else if (activeParticleSlider == 6) {
			drawSliderLabel("FADE", particleFadeRect);
		} else if (activeParticleSlider == 7) {
			drawSliderLabel("NOISE", particleNoiseRect);
		} else if (activeParticleSlider == 8) {
			drawSliderLabel("NOISE START", particleNoiseStartRect);
		}
	} else if (draggingCycleDuration || cycleDurationRect.inside(mx, my)) {
		drawSliderLabel("CYCLE TIME", cycleDurationRect);
	} else if (draggingCycleWindow || cycleWindowRect.inside(mx, my)) {
		drawSliderLabel("TRIGGER WINDOW", cycleWindowRect);
	} else if (presetTransitionRect.inside(mx, my)) {
		drawSliderLabel("TRANSITION", presetTransitionRect);
	} else {
		if (particleSizeRect.inside(mx, my)) {
			drawSliderLabel("SIZE", particleSizeRect);
		} else if (particleSpeedRect.inside(mx, my)) {
			drawSliderLabel("SPEED", particleSpeedRect);
		} else if (particleBounceRect.inside(mx, my)) {
			drawSliderLabel("BOUNCE", particleBounceRect);
		} else if (particleAmountRect.inside(mx, my)) {
			drawSliderLabel("AMOUNT", particleAmountRect);
		} else if (particleLifeRect.inside(mx, my)) {
			drawSliderLabel("LIFE", particleLifeRect);
		} else if (particleTrailRect.inside(mx, my)) {
			drawSliderLabel("TRAIL", particleTrailRect);
		} else if (particleFadeRect.inside(mx, my)) {
			drawSliderLabel("FADE", particleFadeRect);
		} else if (particleNoiseRect.inside(mx, my)) {
			drawSliderLabel("NOISE", particleNoiseRect);
		} else if (particleNoiseStartRect.inside(mx, my)) {
			drawSliderLabel("NOISE START", particleNoiseStartRect);
		}
	}

	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::createParticleSystem(){
	ParticleSystem system;
	system.emitterRect = ofRectangle(outputWidth * 0.25f, 0.0f, outputWidth * 0.5f, 60.0f);
	system.spawnRate = 30.0f;
	system.size = 3.0f;
	system.speed = 220.0f;
	system.bounce = 0.5f;
	system.maxParticles = 360.0f;
	system.lifeSpanMin = 2.0f;
	system.lifeSpanMax = 8.0f;
	system.trail = 0.0f;
	system.fade = 1.0f;
	system.noise = 0.0f;
	system.noiseStart = 0.5f;
	particleSystems.push_back(system);
	selectedParticleIndex = static_cast<int>(particleSystems.size()) - 1;
}

//--------------------------------------------------------------
void ofApp::removeSelectedParticleSystem(){
	if (selectedParticleIndex < 0 || selectedParticleIndex >= static_cast<int>(particleSystems.size())) {
		return;
	}

	particleSystems.erase(particleSystems.begin() + selectedParticleIndex);
	if (particleSystems.empty()) {
		selectedParticleIndex = -1;
	} else {
		selectedParticleIndex = ofClamp(selectedParticleIndex, 0, static_cast<int>(particleSystems.size()) - 1);
	}
}

//--------------------------------------------------------------
void ofApp::updateParticles(float dt){
	if (particleSystems.empty() || !particleGroupEnabled) {
		return;
	}

	struct FoamBounceRegion {
		float left = 0.0f;
		float right = 0.0f;
		float lineY = 0.0f;
	};

	std::vector<FoamBounceRegion> bounceRegions;
	if (foamGroupEnabled && !foamLayers.empty()) {
		bounceRegions.reserve(foamLayers.size());
		for (const auto &foam : foamLayers) {
			if (!foam.enabled || foam.useMist) {
				continue;
			}
			const float widthRatio = ofClamp(foam.bounceOffset, 0.0f, 1.0f);
			if (widthRatio <= 0.0f) {
				continue;
			}
			const float bounceWidth = foam.size.x * widthRatio;
			const float bounceCenter = foam.position.x + foam.size.x * 0.5f;
			FoamBounceRegion region;
			region.left = bounceCenter - bounceWidth * 0.5f;
			region.right = bounceCenter + bounceWidth * 0.5f;
			region.lineY = foam.position.y + foam.size.y * 0.25f;
			bounceRegions.push_back(region);
		}
	}

	const bool maskActive = ndiEnabled && ndiReceiver.ReceiverConnected() && maskPixelsReady
		&& ndiPixels.isAllocated() && ndiPixelsPrev.isAllocated();
	const ofRectangle ndiRect = getNdiOutputRect();
	const int maskW = ndiPixels.getWidth();
	const int maskH = ndiPixels.getHeight();
	const int maskChannels = ndiPixels.getNumChannels();
	const unsigned char *maskData = ndiPixels.getData();
	const unsigned char *maskPrevData = ndiPixelsPrev.getData();
	const float maskThreshold = 0.65f;
	const int maskSearchRadius = 6;
	const float noiseTime = ofGetElapsedTimef() * 0.6f;

	for (auto &system : particleSystems) {
		if (!system.enabled) {
			continue;
		}
		if (particleSpawnEnabled) {
			system.spawnAccumulator += system.spawnRate * dt;
		} else {
			system.spawnAccumulator = 0.0f;
		}
		if (system.spawnAccumulator > system.maxParticles) {
			system.spawnAccumulator = system.maxParticles;
		}
		if (particleSpawnEnabled) {
			const int spawnCount = static_cast<int>(system.spawnAccumulator);
			const size_t maxParticles = static_cast<size_t>(system.maxParticles);
			const size_t available = (system.particles.size() < maxParticles)
				? (maxParticles - system.particles.size())
				: 0;
			const int spawnNow = static_cast<int>(std::min<size_t>(available, static_cast<size_t>(std::max(0, spawnCount))));
			if (spawnNow > 0) {
				system.spawnAccumulator -= spawnNow;
				for (int i = 0; i < spawnNow; ++i) {
					Particle particle;
					particle.position.set(
						ofRandom(system.emitterRect.getMinX(), system.emitterRect.getMaxX()),
						ofRandom(system.emitterRect.getMinY(), system.emitterRect.getMaxY()));
					particle.velocity.set(ofRandom(-20.0f, 20.0f), system.speed);
					particle.prevPos = particle.position;
					particle.trailPos = particle.position;
					particle.prevTrailPos = particle.position;
					particle.trailDelta.set(0.0f, 0.0f);
					particle.trailHead = 0;
					particle.trailCount = 1;
					particle.trail[0] = particle.position;
					particle.trailColorIndex = system.trailColorIndex;
					particle.age = 0.0f;
					const float baseLife = ofRandom(system.lifeSpanMin, system.lifeSpanMax);
					const float travelLife = (outputHeight / std::max(system.speed, 10.0f)) * 2.5f;
					particle.lifespan = travelLife + baseLife;
					particle.noiseSeed = ofRandom(1000.0f);
					system.particles.push_back(particle);
				}
			}
		}

		const bool trailEnabled = system.trail > 0.0f;
		for (auto &particle : system.particles) {
			particle.prevPos = particle.position;
			if (trailEnabled) {
				particle.prevTrailPos = particle.trailPos;
			}
			particle.velocity.y += system.speed * 0.4f * dt;
			const float noiseStartY = system.noiseStart * outputHeight;
			if (system.noise > 0.001f && particle.position.y >= noiseStartY) {
				const float noiseT = ofClamp((particle.position.y - noiseStartY) / std::max(1.0f, outputHeight - noiseStartY), 0.0f, 1.0f);
				const float noiseAmp = system.noise * noiseT;
				const float noiseVal = ofSignedNoise(particle.noiseSeed, noiseTime);
				const float targetVx = noiseVal * noiseAmp;
				particle.velocity.x += (targetVx - particle.velocity.x) * 0.04f;
			}
			particle.position += particle.velocity * dt;
			particle.age += dt;

			if (trailEnabled) {
				const float jump = particle.position.distance(particle.prevPos);
				if (jump > 80.0f) {
					particle.trailPos = particle.position;
					particle.prevTrailPos = particle.position;
					particle.trailDelta.set(0.0f, 0.0f);
				} else {
					particle.trailPos = particle.prevPos;
					particle.prevTrailPos = particle.trailPos;
					const ofVec2f frameDelta = particle.position - particle.prevPos;
					particle.trailDelta = particle.trailDelta * 0.7f + frameDelta * 0.3f;
				}

				if (jump > 80.0f || particle.trailCount == 0) {
					particle.trailHead = 0;
					particle.trailCount = 1;
					particle.trail[0] = particle.position;
				} else {
					particle.trailHead = (particle.trailHead + 1) % Particle::kTrailPoints;
					particle.trail[particle.trailHead] = particle.position;
					if (particle.trailCount < Particle::kTrailPoints) {
						particle.trailCount++;
					}
				}
			}

			for (const auto &region : bounceRegions) {
				if (particle.position.x >= region.left && particle.position.x <= region.right) {
					if (particle.prevPos.y < region.lineY && particle.position.y >= region.lineY && particle.velocity.y > 0.0f) {
						const float speedMag = particle.velocity.length();
						const float speedFactor = ofClamp(ofMap(speedMag, 40.0f, 600.0f, 0.5f, 1.4f, true), 0.5f, 1.4f);
						particle.position.y = region.lineY;
						particle.velocity.y *= -ofRandom(0.4f, 1.0f) * system.bounce * speedFactor;
						particle.velocity.x += ofRandom(-120.0f, 120.0f) * system.bounce * speedFactor;
						particle.age += particle.lifespan * 0.1f;
					}
				}
			}

			if (maskActive && maskChannels >= 3 && ndiRect.inside(particle.position)) {
				const float u = (particle.position.x - ndiRect.x) / ndiRect.width;
				const float v = (particle.position.y - ndiRect.y) / ndiRect.height;
				const float pu = (particle.prevPos.x - ndiRect.x) / ndiRect.width;
				const float pv = (particle.prevPos.y - ndiRect.y) / ndiRect.height;
				const int xi = ofClamp(static_cast<int>(u * (maskW - 1)), 0, maskW - 1);
				const int yi = ofClamp(static_cast<int>(v * (maskH - 1)), 0, maskH - 1);
				const int xip = ofClamp(static_cast<int>(pu * (maskW - 1)), 0, maskW - 1);
				const int yip = ofClamp(static_cast<int>(pv * (maskH - 1)), 0, maskH - 1);
				const int currIndex = (yi * maskW + xi) * maskChannels;
				const int prevIndex = (yip * maskW + xip) * maskChannels;
				const float currBright = (maskData[currIndex] + maskData[currIndex + 1] + maskData[currIndex + 2]) / (3.0f * 255.0f);
				const float prevBright = (maskPrevData[prevIndex] + maskPrevData[prevIndex + 1] + maskPrevData[prevIndex + 2]) / (3.0f * 255.0f);

				if (prevBright < maskThreshold && currBright >= maskThreshold) {
					float motionSpeed = 0.0f;
					if (maskSearchRadius > 0) {
						float maxBright = 0.0f;
						int bestX = xip;
						int bestY = yip;
						for (int dy = -maskSearchRadius; dy <= maskSearchRadius; ++dy) {
							for (int dx = -maskSearchRadius; dx <= maskSearchRadius; ++dx) {
								const int sx = ofClamp(xi + dx, 0, maskW - 1);
								const int sy = ofClamp(yi + dy, 0, maskH - 1);
								const int sampleIndex = (sy * maskW + sx) * maskChannels;
								const float bright = (maskPrevData[sampleIndex] + maskPrevData[sampleIndex + 1] + maskPrevData[sampleIndex + 2]) / (3.0f * 255.0f);
								if (bright > maxBright) {
									maxBright = bright;
									bestX = sx;
									bestY = sy;
								}
							}
						}
						if (maxBright >= maskThreshold) {
							const ofVec2f motion(static_cast<float>(xi - bestX), static_cast<float>(yi - bestY));
							motionSpeed = motion.length();
						}
					}
					const float motionFactor = ofClamp(ofMap(motionSpeed, 0.0f, maskSearchRadius * 1.5f, 0.6f, 1.8f, true), 0.6f, 1.8f);
					const float speedMag = particle.velocity.length();
					const float speedFactor = ofClamp(ofMap(speedMag, 40.0f, 600.0f, 0.5f, 1.5f, true), 0.5f, 1.5f) * motionFactor;
					particle.position = particle.prevPos;
					if (trailEnabled) {
						particle.trailPos = particle.prevPos;
						particle.prevTrailPos = particle.prevPos;
						particle.trailDelta.set(0.0f, 0.0f);
						particle.trailHead = 0;
						particle.trailCount = 1;
						particle.trail[0] = particle.position;
					}
					particle.velocity.y *= -ofRandom(0.3f, 0.9f) * system.bounce * speedFactor;
					particle.velocity.x += ofRandom(-160.0f, 160.0f) * system.bounce * speedFactor;
					particle.age += particle.lifespan * 0.08f;
				}
			}

			if (particle.position.y > outputHeight + 20.0f || particle.age >= particle.lifespan) {
				if (!particleSpawnEnabled) {
					particle.age = particle.lifespan + 1.0f;
					continue;
				}
				particle.position.set(
					ofRandom(system.emitterRect.getMinX(), system.emitterRect.getMaxX()),
					ofRandom(system.emitterRect.getMinY(), system.emitterRect.getMaxY()));
				particle.velocity.set(ofRandom(-20.0f, 20.0f), system.speed);
				particle.prevPos = particle.position;
				if (trailEnabled) {
					particle.trailPos = particle.position;
					particle.prevTrailPos = particle.position;
					particle.trailDelta.set(0.0f, 0.0f);
					particle.trailHead = 0;
					particle.trailCount = 1;
					particle.trail[0] = particle.position;
				}
				particle.trailColorIndex = system.trailColorIndex;
				particle.age = 0.0f;
				const float baseLife = ofRandom(system.lifeSpanMin, system.lifeSpanMax);
				const float travelLife = (outputHeight / std::max(system.speed, 10.0f)) * 2.5f;
				particle.lifespan = travelLife + baseLife;
				particle.noiseSeed = ofRandom(1000.0f);
			}
		}

		if (!particleSpawnEnabled) {
			system.particles.erase(std::remove_if(system.particles.begin(), system.particles.end(),
				[](const Particle &p) { return p.age > p.lifespan; }), system.particles.end());
		}
	}
}

//--------------------------------------------------------------
void ofApp::drawParticles(){
	if (particleSystems.empty() || !particleGroupEnabled) {
		return;
	}

	auto colorForIndex = [](int index) -> ofColor {
		if (index == 1) {
			return ofColor(200, 70, 70);
		}
		if (index == 2) {
			return ofColor(210, 170, 80);
		}
		if (index == 3) {
			return ofColor(90, 140, 200);
		}
		if (index == 4) {
			return ofColor(90, 170, 110);
		}
		return ofColor(255);
	};

	ofPushStyle();
	ofEnableAlphaBlending();
	for (const auto &system : particleSystems) {
		if (!system.enabled) {
			continue;
		}
		const float alpha = ofClamp(system.fade, 0.0f, 1.0f) * 255.0f;
		const float trailAlpha = alpha;
		const bool trailEnabled = system.trail > 0.0f;
		const int maxCount = Particle::kTrailPoints;
		const int desiredCount = trailEnabled
			? static_cast<int>(ofMap(system.trail, 0.0f, 3.0f, 2.0f, static_cast<float>(maxCount), true))
			: 0;

		ofMesh trailMesh;
		trailMesh.setMode(OF_PRIMITIVE_LINES);
		if (trailEnabled) {
			ofSetLineWidth(system.size * 2.0f);
		}
		for (const auto &particle : system.particles) {
			ofSetColor(255, 255, 255, static_cast<unsigned char>(alpha));
			ofDrawCircle(particle.position.x, particle.position.y, system.size);
			if (trailEnabled) {
				const int count = std::min(particle.trailCount, desiredCount);
				if (count < 2) {
					continue;
				}
				for (int i = 0; i < count - 1; ++i) {
					const int idx0 = (particle.trailHead - i + maxCount) % maxCount;
					const int idx1 = (particle.trailHead - i - 1 + maxCount) % maxCount;
					const ofVec2f &p0 = particle.trail[idx0];
					const ofVec2f &p1 = particle.trail[idx1];
					const float t = static_cast<float>(i + 1) / static_cast<float>(count);
					const float a = trailAlpha * (1.0f - t);
					const ofColor particleTrailColor = colorForIndex(particle.trailColorIndex);
					const ofFloatColor meshColor(
						particleTrailColor.r / 255.0f,
						particleTrailColor.g / 255.0f,
						particleTrailColor.b / 255.0f,
						a / 255.0f);
					trailMesh.addVertex(ofVec3f(p0.x, p0.y, 0.0f));
					trailMesh.addColor(meshColor);
					trailMesh.addVertex(ofVec3f(p1.x, p1.y, 0.0f));
					trailMesh.addColor(meshColor);
				}
			}
		}
		if (trailEnabled && trailMesh.getNumVertices() > 0) {
			trailMesh.draw();
		}
	}
	ofDisableAlphaBlending();
	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::updateSelectedParticleSlider(float mouseX){
	if (selectedParticleIndex < 0 || selectedParticleIndex >= static_cast<int>(particleSystems.size())) {
		return;
	}

	ParticleSystem &system = particleSystems[selectedParticleIndex];
	const float sliderMin = particleSizeRect.x;
	const float sliderMax = particleSizeRect.x + particleSizeRect.width;
	const float t = ofClamp((mouseX - sliderMin) / (sliderMax - sliderMin), 0.0f, 1.0f);

	if (activeParticleSlider == 0) {
		system.size = ofLerp(1.0f, 12.0f, t);
	} else if (activeParticleSlider == 1) {
		system.speed = ofLerp(10.0f, 260.0f, t);
	} else if (activeParticleSlider == 2) {
		system.bounce = ofLerp(0.1f, 0.95f, t);
	} else if (activeParticleSlider == 3) {
		system.maxParticles = ofLerp(240.0f, 1500.0f, t);
		system.spawnRate = ofLerp(80.0f, 800.0f, t);
		system.spawnAccumulator = std::min(system.spawnAccumulator + system.maxParticles * 0.5f, system.maxParticles);
		if (particleSpawnEnabled) {
			const int addCount = static_cast<int>(std::min<double>(200.0, system.maxParticles - system.particles.size()));
			for (int i = 0; i < addCount; ++i) {
				Particle particle;
				particle.position.set(
					ofRandom(system.emitterRect.getMinX(), system.emitterRect.getMaxX()),
					ofRandom(system.emitterRect.getMinY(), static_cast<float>(outputHeight)));
				particle.velocity.set(ofRandom(-20.0f, 20.0f), system.speed);
				particle.prevPos = particle.position;
				particle.trailPos = particle.position;
				particle.prevTrailPos = particle.position;
				particle.trailDelta.set(0.0f, 0.0f);
				particle.trailHead = 0;
				particle.trailCount = 1;
				particle.trail[0] = particle.position;
				particle.trailColorIndex = system.trailColorIndex;
				particle.age = 0.0f;
				const float baseLife = ofRandom(system.lifeSpanMin, system.lifeSpanMax);
				const float travelLife = (outputHeight / std::max(system.speed, 10.0f)) * 2.5f;
				particle.lifespan = travelLife + baseLife;
				particle.noiseSeed = ofRandom(1000.0f);
				system.particles.push_back(particle);
			}
		}
	} else if (activeParticleSlider == 4) {
		system.lifeSpanMin = ofLerp(0.5f, 6.0f, t);
		system.lifeSpanMax = ofLerp(system.lifeSpanMin, system.lifeSpanMin + 20.0f, t);
	} else if (activeParticleSlider == 5) {
		system.trail = ofLerp(0.0f, 3.0f, t);
	} else if (activeParticleSlider == 6) {
		system.fade = t;
	} else if (activeParticleSlider == 7) {
		system.noise = ofLerp(0.0f, 200.0f, t);
	} else if (activeParticleSlider == 8) {
		system.noiseStart = t;
	}
}

//--------------------------------------------------------------
void ofApp::drawSliderLabel(const std::string &label, const ofRectangle &rect) const{
	ofSetColor(200);
	const float labelX = rect.x + rect.width + 8.0f;
	const float labelY = rect.y + rect.height - 1.0f;
	ofDrawBitmapString(label, labelX, labelY);
}

//--------------------------------------------------------------
void ofApp::startPresetTransition(int presetIndex){
	if (presetIndex <= 0 || presetIndex > static_cast<int>(presetRects.size())) {
		return;
	}
	if (presetTransitionDuration <= 0.0f) {
		loadComposition(getPresetPath(presetIndex));
		currentPresetIndex = presetIndex;
		sendOscPreset(presetIndex);
		presetTransitionAlpha = 1.0f;
		isPresetTransition = false;
		return;
	}
	isPresetTransition = true;
	pendingPresetIndex = presetIndex;
	presetTransitionAlpha = 1.0f;
	presetTransitionPhase = 0.0f;
	presetTransitionLoaded = false;
	presetTransitionOscSent = false;
	pendingKeepParticles = !particleSystems.empty();
}

//--------------------------------------------------------------
void ofApp::updatePresetTransition(float dt){
	if (!isPresetTransition) {
		presetTransitionAlpha = 1.0f;
		return;
	}
	const float holdDuration = std::max(0.0f, presetTransitionDuration * presetTransitionHoldRatio);
	const float fadeDuration = std::max(0.001f, (presetTransitionDuration - holdDuration) * 0.5f);
	const float holdHalf = holdDuration * 0.5f;
	presetTransitionPhase += dt;
	if (presetTransitionPhase <= fadeDuration) {
		presetTransitionAlpha = ofClamp(1.0f - (presetTransitionPhase / fadeDuration), 0.0f, 1.0f);
		return;
	}
	if (!presetTransitionOscSent && pendingPresetIndex > 0) {
		sendOscPreset(pendingPresetIndex);
		presetTransitionOscSent = true;
	}
	if (presetTransitionPhase <= fadeDuration + holdHalf) {
		presetTransitionAlpha = 0.0f;
		return;
	}
	if (!presetTransitionLoaded && pendingPresetIndex > 0) {
		const bool keepParticles = pendingKeepParticles;
		loadComposition(getPresetPath(pendingPresetIndex), keepParticles, false);
		currentPresetIndex = pendingPresetIndex;
		pendingPresetIndex = 0;
		presetTransitionLoaded = true;
	}
	if (presetTransitionPhase <= fadeDuration + holdDuration) {
		presetTransitionAlpha = 0.0f;
		return;
	}
	const float upPhase = presetTransitionPhase - (fadeDuration + holdDuration);
	presetTransitionAlpha = ofClamp(upPhase / fadeDuration, 0.0f, 1.0f);
	if (upPhase >= fadeDuration) {
		isPresetTransition = false;
		presetTransitionPhase = 0.0f;
		presetTransitionAlpha = 1.0f;
		presetTransitionLoaded = false;
	}
}

//--------------------------------------------------------------
void ofApp::drawNdiTestPattern(){
	ofPushStyle();
	ofSetColor(35, 35, 35, 255);
	ofDrawRectangle(0.0f, 0.0f, outputWidth, outputHeight);

	const std::string label = "ROTOR STUDIO";
	const std::string topLabel = "PANTALLA ARRIBA";
	const std::string bottomLabel = "PANTALLA ABAJO";
	ofSetColor(230);
	if (ndiTestFont.isLoaded()) {
		const ofRectangle labelBounds = ndiTestFont.getStringBoundingBox(label, 0.0f, 0.0f);
		const float labelX = (outputWidth - labelBounds.width) * 0.5f;
		const float labelY = outputHeight * 0.5f + labelBounds.height * 0.5f;
		ndiTestFont.drawString(label, labelX, labelY);

		const ofRectangle topBounds = ndiTestFont.getStringBoundingBox(topLabel, 0.0f, 0.0f);
		const float topX = (outputWidth - topBounds.width) * 0.5f;
		const float topY = outputHeight * 0.25f + topBounds.height * 0.5f;
		ndiTestFont.drawString(topLabel, topX, topY);

		const ofRectangle bottomBounds = ndiTestFont.getStringBoundingBox(bottomLabel, 0.0f, 0.0f);
		const float bottomX = (outputWidth - bottomBounds.width) * 0.5f;
		const float bottomY = outputHeight * 0.75f + bottomBounds.height * 0.5f;
		ndiTestFont.drawString(bottomLabel, bottomX, bottomY);
	} else {
		const float textW = label.size() * 8.0f;
		ofDrawBitmapString(label, (outputWidth - textW) * 0.5f, outputHeight * 0.5f + 6.0f);
		ofDrawBitmapString(topLabel, (outputWidth - topLabel.size() * 8.0f) * 0.5f, outputHeight * 0.25f + 6.0f);
		ofDrawBitmapString(bottomLabel, (outputWidth - bottomLabel.size() * 8.0f) * 0.5f, outputHeight * 0.75f + 6.0f);
	}

	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::resetComposition(){
	selectSenderIndex(0);
	foamLayers.clear();
	selectedFoamIndex = -1;
	particleSystems.clear();
	selectedParticleIndex = -1;
	ndiPosition.set(0.0f, 0.0f);
	ndiPlacementInitialized = false;
	saveComposition();
}

//--------------------------------------------------------------
void ofApp::saveComposition(){
	saveComposition(compositionPath, true);
}

void ofApp::saveComposition(const std::string &path){
	saveComposition(path, false);
}

void ofApp::saveComposition(const std::string &path, bool includeGlobals){
	ofDirectory presetsDir(ofToDataPath("presets", true));
	if (!presetsDir.exists()) {
		presetsDir.create(true);
	}

	ofJson data;
	data["version"] = 1;
	data["output"]["width"] = outputWidth;
	data["output"]["height"] = outputHeight;

	if (includeGlobals) {
		std::string senderName = ndiDesiredSenderName;
		if (senderName.empty() && selectedSenderIndex > 0 &&
			selectedSenderIndex < static_cast<int>(ndiSenders.size())) {
			senderName = ndiSenders[selectedSenderIndex];
		}
		data["ndi"]["sender"] = senderName;
		data["ndi"]["senderIndex"] = selectedSenderIndex;
		data["presetTransitionDuration"] = presetTransitionDuration;
		data["sequence"]["duration"] = cycleDuration;
		data["sequence"]["window"] = cycleTriggerWidth;
	}
	data["ndi"]["enabled"] = ndiEnabled;
	data["ndi"]["fade"] = ndiFade;
	data["foamEnabled"] = foamGroupEnabled;
	data["foamMistSpeed"] = mistSpeed;
	data["ndi"]["position"] = { {"x", ndiPosition.x}, {"y", ndiPosition.y} };

	ofJson foamArray = ofJson::array();
	for (const auto &layer : foamLayers) {
		foamArray.push_back({
			{"x", layer.position.x},
			{"y", layer.position.y},
			{"w", layer.size.x},
			{"h", layer.size.y},
			{"fade", layer.fade},
			{"bounceOffset", layer.bounceOffset},
			{"timeOffset", layer.timeOffset},
			{"useMist", layer.useMist},
			{"enabled", layer.enabled}
			,{"locked", layer.locked}
		});
	}
	data["foam"] = foamArray;

	ofJson particleArray = ofJson::array();
	for (const auto &system : particleSystems) {
		particleArray.push_back({
			{"emitter", {
				{"x", system.emitterRect.x},
				{"y", system.emitterRect.y},
				{"w", system.emitterRect.width},
				{"h", system.emitterRect.height}
			}},
			{"size", system.size},
			{"speed", system.speed},
			{"bounce", system.bounce},
			{"maxParticles", system.maxParticles},
			{"lifeSpanMin", system.lifeSpanMin},
			{"lifeSpanMax", system.lifeSpanMax},
			{"trail", system.trail},
			{"fade", system.fade},
			{"noise", system.noise},
			{"noiseStart", system.noiseStart},
			{"enabled", system.enabled},
			{"locked", system.locked}
		});
	}
	data["particles"] = particleArray;
	data["particlesEnabled"] = particleGroupEnabled;

	ofSavePrettyJson(ofToDataPath(path, true), data);
}

//--------------------------------------------------------------
void ofApp::loadComposition(){
	loadComposition(compositionPath, false, true);
	currentPresetIndex = 0;
}

void ofApp::loadComposition(const std::string &path){
	loadComposition(path, false, false);
}

void ofApp::loadComposition(const std::string &path, bool keepParticles, bool applyGlobals){
	const std::string resolvedPath = ofToDataPath(path, true);
	if (!ofFile::doesFileExist(resolvedPath)) {
		return;
	}

	ofJson data = ofLoadJson(resolvedPath);
	if (data.is_null()) {
		return;
	}

	if (applyGlobals) {
		refreshNdiSenders();
		const std::string senderName = data.value("ndi", ofJson::object()).value("sender", "");
		int senderIndex = 0;
		ndiDesiredSenderName = senderName;
		if (!senderName.empty()) {
			for (int i = 0; i < static_cast<int>(ndiSenders.size()); ++i) {
				if (ndiSenders[i] == senderName) {
					senderIndex = i;
					break;
				}
			}
		}
		if (senderIndex == 0 && data["ndi"].contains("senderIndex")) {
			const int savedIndex = data["ndi"].value("senderIndex", 0);
			if (savedIndex >= 0 && savedIndex < static_cast<int>(ndiSenders.size())) {
				senderIndex = savedIndex;
			}
		}
		selectSenderIndex(senderIndex);
		presetTransitionDuration = data.value("presetTransitionDuration", presetTransitionDuration);
		if (data.contains("sequence")) {
			cycleDuration = data["sequence"].value("duration", cycleDuration);
			cycleTriggerWidth = data["sequence"].value("window", cycleTriggerWidth);
		}
	}
	ndiEnabled = data.value("ndi", ofJson::object()).value("enabled", true);
	ndiFade = data.value("ndi", ofJson::object()).value("fade", 1.0f);

	if (data.contains("ndi") && data["ndi"].contains("position")) {
		ndiPosition.x = data["ndi"]["position"].value("x", ndiPosition.x);
		ndiPosition.y = data["ndi"]["position"].value("y", ndiPosition.y);
		ndiPlacementInitialized = true;
	}

	foamGroupEnabled = data.value("foamEnabled", true);
	mistSpeed = data.value("foamMistSpeed", 1.0f);
	foamLayers.clear();
	if (data.contains("foam")) {
		for (const auto &item : data["foam"]) {
			FoamLayer layer;
			layer.position.set(item.value("x", 0.0f), item.value("y", 0.0f));
			layer.size.set(item.value("w", 400.0f), item.value("h", 300.0f));
			layer.fade = item.value("fade", 0.85f);
			layer.bounceOffset = item.value("bounceOffset", 1.0f);
			layer.timeOffset = item.value("timeOffset", ofRandom(1000.0f));
			layer.useMist = item.value("useMist", false);
			layer.enabled = item.value("enabled", true);
			layer.locked = item.value("locked", false);
			const int fboW = std::max(1, static_cast<int>(layer.size.x * foamRenderScale));
			const int fboH = std::max(1, static_cast<int>(layer.size.y * foamRenderScale));
			layer.fbo.allocate(fboW, fboH, GL_RGBA);
			layer.fbo.begin();
			ofClear(0, 0, 0, 0);
			layer.fbo.end();
			foamLayers.push_back(layer);
		}
	}
	selectedFoamIndex = foamLayers.empty() ? -1 : 0;

	if (!keepParticles) {
	particleGroupEnabled = data.value("particlesEnabled", true);
		particleSystems.clear();
		if (data.contains("particles")) {
			for (const auto &item : data["particles"]) {
				ParticleSystem system;
				if (item.contains("emitter")) {
					system.emitterRect.set(
						item["emitter"].value("x", outputWidth * 0.25f),
						item["emitter"].value("y", 0.0f),
						item["emitter"].value("w", outputWidth * 0.5f),
						item["emitter"].value("h", 60.0f));
				} else {
					system.emitterRect = ofRectangle(outputWidth * 0.25f, 0.0f, outputWidth * 0.5f, 60.0f);
				}
				system.size = item.value("size", 3.0f);
				system.speed = item.value("speed", 220.0f);
				system.bounce = item.value("bounce", 0.5f);
				system.maxParticles = item.value("maxParticles", 360.0f);
				system.lifeSpanMin = item.value("lifeSpanMin", 2.0f);
				system.lifeSpanMax = item.value("lifeSpanMax", 8.0f);
				system.trail = item.value("trail", 0.0f);
				system.fade = item.value("fade", 1.0f);
				system.noise = item.value("noise", 0.0f);
				system.noiseStart = item.value("noiseStart", 0.5f);
				system.enabled = item.value("enabled", true);
			system.locked = item.value("locked", false);
				system.spawnRate = ofMap(system.maxParticles, 240.0f, 1500.0f, 80.0f, 800.0f, true);
				system.spawnAccumulator = 0.0f;
				system.particles.clear();
			const int prefill = particleSpawnEnabled
				? static_cast<int>(std::min<double>(150.0, system.maxParticles * 0.3f))
				: 0;
			for (int i = 0; i < prefill; ++i) {
				Particle particle;
					particle.position.set(
						ofRandom(system.emitterRect.getMinX(), system.emitterRect.getMaxX()),
						ofRandom(system.emitterRect.getMinY(), static_cast<float>(outputHeight)));
				particle.velocity.set(ofRandom(-20.0f, 20.0f), system.speed);
				particle.prevPos = particle.position;
				particle.trailPos = particle.position;
				particle.prevTrailPos = particle.position;
				particle.trailDelta.set(0.0f, 0.0f);
				particle.trailHead = 0;
				particle.trailCount = 1;
				particle.trail[0] = particle.position;
				particle.trailColorIndex = system.trailColorIndex;
				particle.age = 0.0f;
				const float baseLife = ofRandom(system.lifeSpanMin, system.lifeSpanMax);
					const float travelLife = (outputHeight / std::max(system.speed, 10.0f)) * 2.5f;
					particle.lifespan = travelLife + baseLife;
					particle.noiseSeed = ofRandom(1000.0f);
			system.particles.push_back(particle);
			}
			particleSystems.push_back(system);
		}
	}
		selectedParticleIndex = particleSystems.empty() ? -1 : 0;
		if (!particleSystems.empty()) {
			currentColorIndex = particleSystems[0].trailColorIndex;
		}
	}
}
//--------------------------------------------------------------
void ofApp::createFoamLayer(){
	if (!foamShader.isLoaded()) {
		return;
	}

	FoamLayer layer;
	if (foamUseMist) {
		layer.size.set(400.0f, 900.0f);
		layer.position.set((outputWidth - layer.size.x) * 0.5f, 0.0f);
	} else {
		layer.size.set(400.0f, 300.0f);
		layer.position.set((outputWidth - layer.size.x) * 0.5f, (outputHeight - layer.size.y) * 0.5f);
	}
	layer.bounceOffset = 1.0f;
	layer.timeOffset = ofRandom(1000.0f);
	layer.useMist = foamUseMist;
	const int fboW = std::max(1, static_cast<int>(layer.size.x * foamRenderScale));
	const int fboH = std::max(1, static_cast<int>(layer.size.y * foamRenderScale));
	layer.fbo.allocate(fboW, fboH, GL_RGBA);
	layer.fbo.begin();
	ofClear(0, 0, 0, 0);
	layer.fbo.end();

	foamLayers.push_back(layer);
	selectedFoamIndex = static_cast<int>(foamLayers.size()) - 1;
}

//--------------------------------------------------------------
void ofApp::removeSelectedFoamLayer(){
	if (selectedFoamIndex < 0 || selectedFoamIndex >= static_cast<int>(foamLayers.size())) {
		return;
	}

	foamLayers.erase(foamLayers.begin() + selectedFoamIndex);
	if (foamLayers.empty()) {
		selectedFoamIndex = -1;
		draggingFade = false;
	} else {
		selectedFoamIndex = ofClamp(selectedFoamIndex, 0, static_cast<int>(foamLayers.size()) - 1);
	}
}

//--------------------------------------------------------------
void ofApp::updateFoamLayers(){
	if (!foamGroupEnabled) {
		return;
	}
	if (foamUpdateInterval > 1 && (ofGetFrameNum() % foamUpdateInterval != 0)) {
		return;
	}
	if (!foamShader.isLoaded()) {
		return;
	}
	if (!mistShader.isLoaded()) {
		return;
	}

	const float time = ofGetElapsedTimef();
	for (auto &layer : foamLayers) {
		if (!layer.fbo.isAllocated() || !layer.enabled) {
			continue;
		}
		layer.fbo.begin();
		ofClear(0, 0, 0, 0);
		ofShader &shader = layer.useMist ? mistShader : foamShader;
		shader.begin();
		shader.setUniform2f("u_resolution", layer.fbo.getWidth(), layer.fbo.getHeight());
		shader.setUniform1f("u_time", time + layer.timeOffset);
		shader.setUniform1f("u_intensity", layer.fade);
		if (layer.useMist) {
			shader.setUniform1f("u_speed", mistSpeed);
		}
		ofDrawRectangle(0.0f, 0.0f, layer.fbo.getWidth(), layer.fbo.getHeight());
		shader.end();
		layer.fbo.end();
	}
}

//--------------------------------------------------------------
bool ofApp::hitTestFoamLayer(const ofVec2f &outputPos, int &hitIndex) const{
	if (!foamGroupEnabled) {
		return false;
	}
	for (int i = static_cast<int>(foamLayers.size()) - 1; i >= 0; --i) {
		const FoamLayer &layer = foamLayers[i];
		if (!layer.enabled) {
			continue;
		}
		if (layer.locked) {
			continue;
		}
		ofRectangle rect(layer.position.x, layer.position.y, layer.size.x, layer.size.y);
		if (rect.inside(outputPos)) {
			hitIndex = i;
			return true;
		}
	}
	return false;
}

//--------------------------------------------------------------
ofVec2f ofApp::windowToOutput(const ofVec2f &windowPos) const{
	if (!outputFbo.isAllocated()) {
		return ofVec2f();
	}

	if (previewRect.getWidth() <= 0.0f || previewRect.getHeight() <= 0.0f) {
		return ofVec2f();
	}

	const float u = (windowPos.x - previewRect.x) / previewRect.width;
	const float v = (windowPos.y - previewRect.y) / previewRect.height;
	return ofVec2f(u * outputWidth, v * outputHeight);
}

//--------------------------------------------------------------
ofRectangle ofApp::outputToWindowRect(const ofRectangle &outputRect) const{
	if (!outputFbo.isAllocated() || previewRect.getWidth() <= 0.0f || previewRect.getHeight() <= 0.0f) {
		return ofRectangle();
	}

	const float sx = previewRect.getWidth() / static_cast<float>(outputWidth);
	const float sy = previewRect.getHeight() / static_cast<float>(outputHeight);
	return ofRectangle(
		previewRect.x + outputRect.x * sx,
		previewRect.y + outputRect.y * sy,
		outputRect.getWidth() * sx,
		outputRect.getHeight() * sy);
}

//--------------------------------------------------------------
void ofApp::updateSelectedFoamFade(float mouseX){
	if (selectedFoamIndex < 0 || selectedFoamIndex >= static_cast<int>(foamLayers.size())) {
		return;
	}

	const float t = ofClamp((mouseX - fadeSliderRect.x) / fadeSliderRect.width, 0.0f, 1.0f);
	foamLayers[selectedFoamIndex].fade = t;
}

//--------------------------------------------------------------
void ofApp::updateSelectedFoamBounce(float mouseX){
	if (selectedFoamIndex < 0 || selectedFoamIndex >= static_cast<int>(foamLayers.size())) {
		return;
	}

	const float t = ofClamp((mouseX - foamBounceRect.x) / foamBounceRect.width, 0.0f, 1.0f);
	foamLayers[selectedFoamIndex].bounceOffset = t;
}

//--------------------------------------------------------------
void ofApp::updateNdiPlacement(){
	if (!ndiTexture.isAllocated()) {
		return;
	}

	const float texW = ndiTexture.getWidth();
	const float texH = ndiTexture.getHeight();
	if (texW <= 0.0f || texH <= 0.0f) {
		return;
	}

	const float scale = std::min(outputWidth / texW, outputHeight / texH);
	const ofVec2f newSize(texW * scale, texH * scale);
	if (!ndiPlacementInitialized || newSize != ndiSize) {
		ndiSize = newSize;
		ndiPosition.set((outputWidth - ndiSize.x) * 0.5f, (outputHeight - ndiSize.y) * 0.5f);
		ndiPlacementInitialized = true;
	}
}

//--------------------------------------------------------------
ofRectangle ofApp::getNdiOutputRect() const{
	if (!ndiTexture.isAllocated()) {
		return ofRectangle();
	}

	return ofRectangle(ndiPosition.x, ndiPosition.y, ndiSize.x, ndiSize.y);
}

//--------------------------------------------------------------
void ofApp::drawTextureFitted(const ofTexture &texture, const ofRectangle &bounds){
	if (!texture.isAllocated()) {
		return;
	}

	ofPushStyle();
	ofSetColor(255);
	ofRectangle src(0.0f, 0.0f, texture.getWidth(), texture.getHeight());
	src.scaleTo(bounds, OF_SCALEMODE_FIT);
	src.setPosition(bounds.getCenter().x - src.getCenter().x,
		bounds.getCenter().y - src.getCenter().y);

	texture.draw(src);
	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::updatePreviewRect(){
	if (!outputFbo.isAllocated()) {
		return;
	}

	const float texW = static_cast<float>(outputWidth);
	const float texH = static_cast<float>(outputHeight);
	if (texW <= 0.0f || texH <= 0.0f) {
		return;
	}

	const float sidebarWidth = getUiSidebarWidth();
	const float sidebarGap = showGui ? 16.0f : 0.0f;
	const float usableWidth = std::max(1.0f, ofGetWidth() - sidebarWidth - sidebarGap);
	const ofRectangle bounds(sidebarWidth + sidebarGap, 0.0f, usableWidth, ofGetHeight());
	ofRectangle fit(0.0f, 0.0f, texW, texH);
	fit.scaleTo(bounds, OF_SCALEMODE_FIT);

	previewRect = fit;
	previewRect.setPosition(
		bounds.x + (bounds.getWidth() - fit.getWidth()),
		bounds.y + (bounds.getHeight() - fit.getHeight()) * 0.5f);
	previewRectInitialized = true;
}

//--------------------------------------------------------------
std::string ofApp::getPresetPath(int index) const{
	const int clamped = ofClamp(index, 1, 5);
	return "presets/composition_" + ofToString(clamped) + ".json";
}

//--------------------------------------------------------------
float ofApp::getUiSidebarWidth() const{
	if (!showGui) {
		return 0.0f;
	}
	return gui.getShape().getWidth() + 20.0f;
}

//--------------------------------------------------------------
void ofApp::sendOscPreset(int presetIndex){
	if (!oscEnabled) {
		return;
	}
	if (presetIndex <= 0) {
		return;
	}
	ofxOscMessage msg;
	msg.setAddress("/preset" + ofToString(presetIndex));
	oscSender.sendMessage(msg, false);
}

//--------------------------------------------------------------
void ofApp::sendOscColor(int colorIndex){
	if (!oscEnabled) {
		return;
	}
	ofxOscMessage msg;
	msg.setAddress("/color" + ofToString(colorIndex));
	oscSender.sendMessage(msg, false);
}

//--------------------------------------------------------------
void ofApp::triggerCycleEvent(){
	const int presetIndex = static_cast<int>(ofRandom(1, 6));
	const int colorIndex = static_cast<int>(ofRandom(0, 5));

	startPresetTransition(presetIndex);

	for (auto &system : particleSystems) {
		system.trailColorIndex = colorIndex;
	}
	currentColorIndex = colorIndex;
	sendOscColor(colorIndex);
}

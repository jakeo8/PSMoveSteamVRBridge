#define _USE_MATH_DEFINES

#include "constants.h"
#include "server_driver.h"
#include "utils.h"
#include "settings_util.h"
#include "driver.h"
#include "facing_handsolver.h"
#include "ps_move_controller.h"
#include "trackable_device.h"
#include <assert.h>

#if _MSC_VER
#define strcasecmp(a, b) stricmp(a,b)
#pragma warning (disable: 4996) // 'This function or variable may be unsafe': snprintf
#define snprintf _snprintf
#endif

namespace steamvrbridge {

	PSMoveController::PSMoveController(
		PSMControllerID psmControllerId,
		vr::ETrackedControllerRole trackedControllerRole,
		const char *psmSerialNo)
		: Controller()
		, m_nPSMControllerId(psmControllerId)
		, m_PSMServiceController(nullptr)
		, m_nPoseSequenceNumber(0)
		, m_bIsBatteryCharging(false)
		, m_fBatteryChargeFraction(0.f)
		, m_bRumbleSuppressed(false)
		, m_pendingHapticDurationSecs(DEFAULT_HAPTIC_DURATION)
		, m_pendingHapticAmplitude(DEFAULT_HAPTIC_AMPLITUDE)
		, m_pendingHapticFrequency(DEFAULT_HAPTIC_FREQUENCY)
		, m_lastTimeRumbleSent()
		, m_lastTimeRumbleSentValid(false)
		, m_resetPoseButtonPressTime()
		, m_bResetPoseRequestSent(false)
		, m_resetAlignButtonPressTime()
		, m_bResetAlignRequestSent(false)
		, m_fVirtuallExtendControllersZMeters(0.0f)
		, m_fVirtuallExtendControllersYMeters(0.0f)
		, m_fVirtuallyRotateController(false)
		, m_bDelayAfterTouchpadPress(false)
		, m_bTouchpadWasActive(false)
		, m_fControllerMetersInFrontOfHmdAtCalibration(0.f)
		, m_posMetersAtTouchpadPressTime(*k_psm_float_vector3_zero)
		, m_driverSpaceRotationAtTouchpadPressTime(*k_psm_quaternion_identity)
		, m_bDisableHMDAlignmentGesture(false)
		, m_bUseControllerOrientationInHMDAlignment(false)
		, m_steamVRTriggerAxisIndex(1)
		, m_fLinearVelocityMultiplier(1.f)
		, m_fLinearVelocityExponent(0.f)
		, m_overrideModel("") {
		char svrIdentifier[256];
		Utils::GenerateControllerSteamVRIdentifier(svrIdentifier, sizeof(svrIdentifier), psmControllerId);
		m_strSteamVRSerialNo = svrIdentifier;

		m_lastTouchpadPressTime = std::chrono::high_resolution_clock::now();

		if (psmSerialNo != NULL) {
			m_strPSMControllerSerialNo = psmSerialNo;
		}

		// Tell PSM Client API that we are listening to this controller id
		PSM_AllocateControllerListener(psmControllerId);
		m_PSMServiceController = PSM_GetController(psmControllerId);

		m_TrackedControllerRole = trackedControllerRole;

		// Load config from steamvr.vrsettings
		vr::IVRSettings *pSettings = vr::VRSettings();

		// Map every button to not be associated with any touchpad direction, initially
		memset(psButtonIDToEmulatedTouchpadAction, k_EmulatedTrackpadAction_None, k_PSMButtonID_Count * sizeof(vr::EVRButtonId));

		if (pSettings != nullptr) {
			//PSMove controller button -> fake touchpad mappings
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_PS, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Move, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Triangle, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Square, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Circle, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Cross, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Select, psmControllerId);
			LoadEmulatedTouchpadActions(pSettings, k_PSMButtonID_Start, psmControllerId);

			// Touch pad settings
			m_bDelayAfterTouchpadPress =
				SettingsUtil::LoadBool(pSettings, "psmove_touchpad", "delay_after_touchpad_press", m_bDelayAfterTouchpadPress);
			m_fMetersPerTouchpadAxisUnits =
				SettingsUtil::LoadFloat(pSettings, "psmove", "meters_per_touchpad_units", .075f);

			// Throwing power settings
			m_fLinearVelocityMultiplier =
				SettingsUtil::LoadFloat(pSettings, "psmove_settings", "linear_velocity_multiplier", 1.f);
			m_fLinearVelocityExponent =
				SettingsUtil::LoadFloat(pSettings, "psmove_settings", "linear_velocity_exponent", 0.f);

			// General Settings
			m_bRumbleSuppressed = SettingsUtil::LoadBool(pSettings, "psmove_settings", "rumble_suppressed", m_bRumbleSuppressed);
			m_fVirtuallExtendControllersYMeters = SettingsUtil::LoadFloat(pSettings, "psmove_settings", "psmove_extend_y", 0.0f);
			m_fVirtuallExtendControllersZMeters = SettingsUtil::LoadFloat(pSettings, "psmove_settings", "psmove_extend_z", 0.0f);
			m_fVirtuallyRotateController = SettingsUtil::LoadBool(pSettings, "psmove_settings", "psmove_rotate", false);
			m_fControllerMetersInFrontOfHmdAtCalibration =
				SettingsUtil::LoadFloat(pSettings, "psmove", "m_fControllerMetersInFrontOfHmdAtCallibration", 0.06f);
			m_bDisableHMDAlignmentGesture = SettingsUtil::LoadBool(pSettings, "psmove_settings", "disable_alignment_gesture", false);
			m_bUseControllerOrientationInHMDAlignment = SettingsUtil::LoadBool(pSettings, "psmove_settings", "use_orientation_in_alignment", true);

			Logger::Logger::Info("m_fControllerMetersInFrontOfHmdAtCalibration(psmove): %f\n", m_fControllerMetersInFrontOfHmdAtCalibration);
		}

		m_trackingStatus = m_bDisableHMDAlignmentGesture ? vr::TrackingResult_Running_OK : vr::TrackingResult_Uninitialized;

	}

	PSMoveController::~PSMoveController() {
		PSM_FreeControllerListener(m_PSMServiceController->ControllerID);
		m_PSMServiceController = nullptr;
	}

	void PSMoveController::LoadEmulatedTouchpadActions(
		vr::IVRSettings *pSettings,
		const ePSMButtonID psButtonID,
		int controllerId) {

		eEmulatedTrackpadAction vrTouchpadDirection = k_EmulatedTrackpadAction_None;

		if (pSettings != nullptr) {
			vr::EVRSettingsError fetchError;

			const char *szPSButtonName = k_PSMButtonNames[psButtonID];
			const char *szButtonSectionName = "psmove";
			const char *szTouchpadSectionName = "psmove_touchpad_directions";

			char remapButtonToTouchpadDirectionString[32];
			pSettings->GetString(szTouchpadSectionName, szPSButtonName, remapButtonToTouchpadDirectionString, 32, &fetchError);

			if (fetchError == vr::VRSettingsError_None) {
				for (int vr_touchpad_direction_index = 0; vr_touchpad_direction_index < k_max_vr_touchpad_directions; ++vr_touchpad_direction_index) {
					if (strcasecmp(remapButtonToTouchpadDirectionString, k_VRTouchpadDirectionNames[vr_touchpad_direction_index]) == 0) {
						vrTouchpadDirection = static_cast<eEmulatedTrackpadAction>(vr_touchpad_direction_index);
						break;
					}
				}
			}

			if (controllerId >= 0 && controllerId <= 9) {
				char buffer[64];
				snprintf(buffer, sizeof(buffer), "%s_%d", szTouchpadSectionName, controllerId);

				szTouchpadSectionName = buffer;
				pSettings->GetString(szTouchpadSectionName, szPSButtonName, remapButtonToTouchpadDirectionString, 32, &fetchError);

				if (fetchError == vr::VRSettingsError_None) {
					for (int vr_touchpad_direction_index = 0; vr_touchpad_direction_index < k_max_vr_touchpad_directions; ++vr_touchpad_direction_index) {
						if (strcasecmp(remapButtonToTouchpadDirectionString, k_VRTouchpadDirectionNames[vr_touchpad_direction_index]) == 0) {
							vrTouchpadDirection = static_cast<eEmulatedTrackpadAction>(vr_touchpad_direction_index);
							break;
						}
					}
				}
			}
		}

		// Save the mapping
		assert(psButtonID >= 0 && psButtonID < k_PSMButtonID_Count);
		psButtonIDToEmulatedTouchpadAction[psButtonID] = vrTouchpadDirection;
	}

	vr::EVRInitError PSMoveController::Activate(vr::TrackedDeviceIndex_t unObjectId) {
		vr::EVRInitError result = Controller::Activate(unObjectId);

		if (result == vr::VRInitError_None) {
			Logger::Info("CPSMoveControllerLatest::Activate - Controller %d Activated\n", unObjectId);

			g_ServerTrackedDeviceProvider.LaunchPSMoveMonitor();

			PSMRequestID requestId;
			if (PSM_StartControllerDataStreamAsync(
				m_PSMServiceController->ControllerID,
				PSMStreamFlags_includePositionData | PSMStreamFlags_includePhysicsData,
				&requestId) == PSMResult_Success) {
				PSM_RegisterCallback(requestId, PSMoveController::start_controller_response_callback, this);
			}

			// Setup controller properties
			{
				vr::CVRPropertyHelpers *properties = vr::VRProperties();

				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceOff_String, "{psmove}controller_status_off.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearching_String, "{psmove}controller_status_ready.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{psmove}controller_status_ready_alert.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReady_String, "{psmove}controller_status_ready.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{psmove}controller_status_ready_alert.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceNotReady_String, "{psmove}controller_status_error.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceStandby_String, "{psmove}controller_status_ready.png");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceAlertLow_String, "{psmove}controller_status_ready_low.png");

				properties->SetBoolProperty(m_ulPropertyContainer, vr::Prop_WillDriftInYaw_Bool, false);
				properties->SetBoolProperty(m_ulPropertyContainer, vr::Prop_DeviceIsWireless_Bool, true);
				properties->SetBoolProperty(m_ulPropertyContainer, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);

				properties->SetInt32Property(m_ulPropertyContainer, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);

				// The {psmove} syntax lets us refer to rendermodels that are installed
				// in the driver's own resources/rendermodels directory.  The driver can
				// still refer to SteamVR models like "generic_hmd".
				char model_label[32] = "\0";
				snprintf(model_label, sizeof(model_label), "psmove_%d", m_PSMServiceController->ControllerID);
				//properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, "{psmove}psmove_controller");
				properties->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModeLabel_String, model_label);

				// Set device properties
				vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, m_TrackedControllerRole);
				vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ManufacturerName_String, "HTC");

				// Fake Vive for motion controllers
				vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, vr::Prop_HardwareRevision_Uint64, 1313);
				vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, vr::Prop_FirmwareVersion_Uint64, 1315);
				vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, "PS Move");
				vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_SerialNumber_String, m_strPSMControllerSerialNo.c_str());
				vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, "vr_controller_vive_1_5");
				//vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, "{psmove}psmove_controller");
			}

			// Create buttons components
			Controller::CreateButtonComponent(k_PSMButtonID_PS);
			Controller::CreateButtonComponent(k_PSMButtonID_Triangle);
			Controller::CreateButtonComponent(k_PSMButtonID_Circle);
			Controller::CreateButtonComponent(k_PSMButtonID_Cross);
			Controller::CreateButtonComponent(k_PSMButtonID_Square);
			Controller::CreateButtonComponent(k_PSMButtonID_Move);
			Controller::CreateButtonComponent(k_PSMButtonID_Select);
			Controller::CreateButtonComponent(k_PSMButtonID_Start);

			// Create axis components
			Controller::CreateAxisComponent(k_PSMAxisID_Trigger);

			// [optional] Create components for emulated trackpad
			for (int buttonIndex = 0; buttonIndex < static_cast<int>(k_PSMButtonID_Count); ++buttonIndex) {
				if (psButtonIDToEmulatedTouchpadAction[buttonIndex] != k_EmulatedTrackpadAction_None) {
					Controller::CreateButtonComponent(k_PSMButtonID_EmulatedTrackpadTouched);
					Controller::CreateButtonComponent(k_PSMButtonID_EmulatedTrackpadPressed);
					Controller::CreateAxisComponent(k_PSMAxisID_EmulatedTrackpad_X);
					Controller::CreateAxisComponent(k_PSMAxisID_EmulatedTrackpad_Y);
					break;
				}
			}
		}

		return result;
	}

	void PSMoveController::start_controller_response_callback(
		const PSMResponseMessage *response, void *userdata) {
		PSMoveController *controller = reinterpret_cast<PSMoveController *>(userdata);

		if (response->result_code == PSMResult::PSMResult_Success) {
			Logger::Info("CPSMoveControllerLatest::start_controller_response_callback - Controller stream started\n");
		}
	}

	void PSMoveController::Deactivate() {
		Logger::Info("CPSMoveControllerLatest::Deactivate - Controller stream stopped\n");
		PSM_StopControllerDataStreamAsync(m_PSMServiceController->ControllerID, nullptr);
	}

	void PSMoveController::UpdateControllerState() {
		static const uint64_t s_kTouchpadButtonMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad);

		assert(m_PSMServiceController != nullptr);
		assert(m_PSMServiceController->IsConnected);

		const PSMPSMove &clientView = m_PSMServiceController->ControllerState.PSMoveState;

		bool bStartRealignHMDTriggered =
			(clientView.StartButton == PSMButtonState_PRESSED && clientView.SelectButton == PSMButtonState_PRESSED) ||
			(clientView.StartButton == PSMButtonState_PRESSED && clientView.SelectButton == PSMButtonState_DOWN) ||
			(clientView.StartButton == PSMButtonState_DOWN && clientView.SelectButton == PSMButtonState_PRESSED);

		// See if the recenter button has been held for the requisite amount of time
		bool bRecenterRequestTriggered = false;
		{
			PSMButtonState resetPoseButtonState;
			switch (m_TrackedControllerRole) {
				case vr::TrackedControllerRole_LeftHand:
					resetPoseButtonState = clientView.SelectButton;
					break;
				case vr::TrackedControllerRole_RightHand:
					resetPoseButtonState = clientView.StartButton;
					break;
				default:
					resetPoseButtonState = clientView.SelectButton;
					break;
			}

			switch (resetPoseButtonState) {
				case PSMButtonState_PRESSED:
				{
					m_resetPoseButtonPressTime = std::chrono::high_resolution_clock::now();
				} break;
				case PSMButtonState_DOWN:
				{
					if (!m_bResetPoseRequestSent) {
						const float k_hold_duration_milli = 250.f;
						std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();
						std::chrono::duration<float, std::milli> pressDurationMilli = now - m_resetPoseButtonPressTime;

						if (pressDurationMilli.count() >= k_hold_duration_milli) {
							bRecenterRequestTriggered = true;
						}
					}
				} break;
				case PSMButtonState_RELEASED:
				{
					m_bResetPoseRequestSent = false;
				} break;
			}

		}

		// If START was just pressed while and SELECT was held or vice versa,
		// recenter the controller orientation pose and start the realignment of the controller to HMD tracking space.
		if (bStartRealignHMDTriggered && !m_bDisableHMDAlignmentGesture) {
			PSMVector3f controllerBallPointedUpEuler = { (float)M_PI_2, 0.0f, 0.0f };
			PSMQuatf controllerBallPointedUpQuat = PSM_QuatfCreateFromAngles(&controllerBallPointedUpEuler);

			Logger::Info("CPSMoveControllerLatest::UpdateControllerState(): Calling StartRealignHMDTrackingSpace() in response to controller chord.\n");

			PSM_ResetControllerOrientationAsync(m_PSMServiceController->ControllerID, &controllerBallPointedUpQuat, nullptr);
			m_bResetPoseRequestSent = true;

			// We have the transform of the HMD in world space. 
			// However the HMD and the controller aren't quite aligned depending on the controller type:
			PSMQuatf controllerOrientationInHmdSpaceQuat = *k_psm_quaternion_identity;
			PSMVector3f controllerLocalOffsetFromHmdPosition = *k_psm_float_vector3_zero;
			// Rotation) The controller's local -Z axis (from the center to the glowing ball) is currently pointed 
			//    in the direction of the HMD's local +Y axis, 
			// Translation) The controller's position is a few inches ahead of the HMD's on the HMD's local -Z axis. 
			PSMVector3f eulerPitch = { (float)M_PI_2, 0.0f, 0.0f };
			controllerOrientationInHmdSpaceQuat = PSM_QuatfCreateFromAngles(&eulerPitch);
			controllerLocalOffsetFromHmdPosition = { 0.0f, 0.0f, -1.0f * m_fControllerMetersInFrontOfHmdAtCalibration };

			try {
				PSMPosef hmdPose = Utils::GetHMDPoseInMeters();
				PSMPosef realignedPose = Utils::RealignHMDTrackingSpace(controllerOrientationInHmdSpaceQuat,
																		controllerLocalOffsetFromHmdPosition,
																		m_PSMServiceController->ControllerID,
																		hmdPose,
																		m_bUseControllerOrientationInHMDAlignment);
				g_ServerTrackedDeviceProvider.SetHMDTrackingSpace(realignedPose);
			} catch (std::exception & e) {
				// Log an error message and safely carry on
				Logger::Error(e.what());
			}

			m_bResetAlignRequestSent = true;
		} else if (bRecenterRequestTriggered) {
			Logger::Info("CPSMoveControllerLatest::UpdateControllerState(): Calling ClientPSMoveAPI::reset_orientation() in response to controller button press.\n");

			PSM_ResetControllerOrientationAsync(m_PSMServiceController->ControllerID, k_psm_quaternion_identity, nullptr);
			m_bResetPoseRequestSent = true;
		} else {

			// Process all the native buttons 
			Controller::UpdateButton(k_PSMButtonID_Circle, clientView.CircleButton);
			Controller::UpdateButton(k_PSMButtonID_Cross, clientView.CrossButton);
			Controller::UpdateButton(k_PSMButtonID_Move, clientView.MoveButton);
			Controller::UpdateButton(k_PSMButtonID_PS, clientView.PSButton);
			Controller::UpdateButton(k_PSMButtonID_Select, clientView.SelectButton);
			Controller::UpdateButton(k_PSMButtonID_Square, clientView.SquareButton);
			Controller::UpdateButton(k_PSMButtonID_Start, clientView.StartButton);
			Controller::UpdateButton(k_PSMButtonID_Triangle, clientView.TriangleButton);

			// Touchpad handling
			UpdateEmulatedTrackpad();

			// PSMove Trigger handling
			Controller::UpdateAxis(k_PSMAxisID_Trigger, clientView.TriggerValue / 255.f);

			// Update the battery charge state
			UpdateBatteryChargeState(m_PSMServiceController->ControllerState.PSMoveState.BatteryValue);
		}
	}

	/* TODO - Add informative ui overlay in monitor.cpp to show the user how this works.

	In a nutshell, upon the move button being pressed the initial pose is captured and rotated relative to the
	controller's position. After a buttonheld threshold it's considered held and the next controller pose is captured
	and again rotated. The initial and current are subtracted to get the distance in meters between the two. The rotation
	is important since it must be relative to the controller not the world. After the rotation a repeatable calculation of
	distance between the two on the z and x axis can be determined. This is then scaled and applied to the x and y axis
	of the trackpad. When the ps move button is no longer pressed the trackpad axis is reset to 0,0 and past state is
	cleared.

	```
	Initial origin pose:

		z   _
		|  (_)
		|  {0} <- Move button pressed and held facing forward on the y axis
		|  |*|
		|  {_}
		|_________ x
	   /
	  /
	 /
	y


	Future pose update:

		z                 _
		|       7.5cm    (_)
		|     ------->   {0} <- Move button still held facing forward on the x axis
		|      moved     |*|
		|      right     {_}
		|_________ x
	   /
	  /
	 /
	y
	```
	*/

	// Updates the state of the controllers touchpad axis relative to its position over time and active state.
	void PSMoveController::UpdateEmulatedTrackpad() {
		// Bail if the config hasn't enabled the emulated trackpad
		if (!HasButton(k_PSMButtonID_EmulatedTrackpadPressed) && !HasButton(k_PSMButtonID_EmulatedTrackpadPressed))
			return;

		// Find the highest priority emulated touch pad action (if any)
		eEmulatedTrackpadAction highestPriorityAction= k_EmulatedTrackpadAction_None;
		for (int buttonIndex = 0; buttonIndex < static_cast<int>(k_PSMButtonID_Count); ++buttonIndex) {
			eEmulatedTrackpadAction action= psButtonIDToEmulatedTouchpadAction[buttonIndex];
			if (action != k_EmulatedTrackpadAction_None) {
				PSMButtonState button_state= PSMButtonState_UP;
				if (Controller::GetButtonState((ePSMButtonID)buttonIndex, button_state))
				{
					if (action >= highestPriorityAction)
					{
						highestPriorityAction= action;
					}

					if (action >= k_EmulatedTrackpadAction_Press)
					{
						break;
					}
				}
			}
		}

		float touchpad_x = 0.f;
		float touchpad_y = 0.f;
		PSMButtonState emulatedTouchPadTouchedState= PSMButtonState_UP;
		PSMButtonState emulatedTouchPadPressedState= PSMButtonState_UP;

		if (highestPriorityAction == k_EmulatedTrackpadAction_Touch)
		{
			emulatedTouchPadTouchedState= PSMButtonState_DOWN;
		}
		else if (highestPriorityAction == k_EmulatedTrackpadAction_Press)
		{
			emulatedTouchPadTouchedState= PSMButtonState_DOWN;
			emulatedTouchPadPressedState= PSMButtonState_DOWN;
		}

		// If the action specifies a specific trackpad direction,
		// then use the given trackpad axis
		if (highestPriorityAction > k_EmulatedTrackpadAction_Press)
		{
			emulatedTouchPadTouchedState= PSMButtonState_DOWN;
			emulatedTouchPadPressedState= PSMButtonState_DOWN;

			switch (highestPriorityAction)
			{
			case k_EmulatedTrackpadAction_Left:
				touchpad_x= -1.f;
				touchpad_y= 0.f;
				break;
			case k_EmulatedTrackpadAction_Up:
				touchpad_x= 0.f;
				touchpad_y= 1.f;
				break;
			case k_EmulatedTrackpadAction_Right:
				touchpad_x= 1.f;
				touchpad_y= 0.f;
				break;
			case k_EmulatedTrackpadAction_Down:
				touchpad_x= 0.f;
				touchpad_y= -1.f;
				break;
			case k_EmulatedTrackpadAction_UpLeft:
				touchpad_x = -0.707f;
				touchpad_y = 0.707f;
				break;
			case k_EmulatedTrackpadAction_UpRight:
				touchpad_x = 0.707f;
				touchpad_y = 0.707f;
				break;
			case k_EmulatedTrackpadAction_DownLeft:
				touchpad_x = -0.707f;
				touchpad_y = -0.707f;
				break;
			case k_EmulatedTrackpadAction_DownRight:
				touchpad_x = 0.707f;
				touchpad_y = -0.707f;
				break;
			}
		}
		// Otherwise if the action was just a touch or press,
		// then use spatial offset method for determining touchpad axis
		if (highestPriorityAction == k_EmulatedTrackpadAction_Touch || 
			highestPriorityAction == k_EmulatedTrackpadAction_Press) {

			bool bIsNewTouchpadLocation = true;

			if (m_bDelayAfterTouchpadPress) {
				std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();

				if (!m_bTouchpadWasActive) {
					const float k_max_touchpad_press = 2000.0; // time until coordinates are reset, otherwise assume in last location.
					std::chrono::duration<double, std::milli> timeSinceActivated = now - m_lastTouchpadPressTime;

					// true if the touchpad has been active for more than the max time required to hold it
					bIsNewTouchpadLocation = timeSinceActivated.count() >= k_max_touchpad_press;
				}
				m_lastTouchpadPressTime = now;
			}

			if (bIsNewTouchpadLocation) {
				if (!m_bTouchpadWasActive) {
					// Just pressed.
					const PSMPSMove &view = m_PSMServiceController->ControllerState.PSMoveState;
					m_driverSpaceRotationAtTouchpadPressTime = view.Pose.Orientation;

					Utils::GetMetersPosInRotSpace(&m_driverSpaceRotationAtTouchpadPressTime, &m_posMetersAtTouchpadPressTime, m_PSMServiceController->ControllerState.PSMoveState);

					#if LOG_TOUCHPAD_EMULATION != 0
					Logger::Info("Touchpad pressed! At (%f, %f, %f) meters relative to orientation\n",
									m_posMetersAtTouchpadPressTime.x, m_posMetersAtTouchpadPressTime.y, m_posMetersAtTouchpadPressTime.z);
					#endif
				} else {
					// Held!

					PSMVector3f newPosMeters;
					Utils::GetMetersPosInRotSpace(&m_driverSpaceRotationAtTouchpadPressTime, &newPosMeters, m_PSMServiceController->ControllerState.PSMoveState);

					PSMVector3f offsetMeters = PSM_Vector3fSubtract(&newPosMeters, &m_posMetersAtTouchpadPressTime);

					#if LOG_TOUCHPAD_EMULATION != 0
					Logger::Info("Touchpad held! Relative position (%f, %f, %f) meters\n",
									offsetMeters.x, offsetMeters.y, offsetMeters.z);
					#endif

					touchpad_x = fminf(fmaxf(offsetMeters.x / m_fMetersPerTouchpadAxisUnits, -1.0f), 1.0f);
					touchpad_y = fminf(fmaxf(-offsetMeters.z / m_fMetersPerTouchpadAxisUnits, -1.0f), 1.0f);

					#if LOG_TOUCHPAD_EMULATION != 0
					Logger::Info("Touchpad axis at (%f, %f) \n", touchpad_x, touchpad_x);
					#endif
				}
			}
		}

		Controller::UpdateButton(k_PSMButtonID_EmulatedTrackpadTouched, emulatedTouchPadTouchedState);
		Controller::UpdateButton(k_PSMButtonID_EmulatedTrackpadPressed, emulatedTouchPadPressedState);

		Controller::UpdateAxis(k_PSMAxisID_EmulatedTrackpad_X, touchpad_x);
		Controller::UpdateAxis(k_PSMAxisID_EmulatedTrackpad_Y, touchpad_y);

		// Remember if the touchpad was active the previous frame for edge detection
		m_bTouchpadWasActive = highestPriorityAction != k_EmulatedTrackpadAction_None;
	}

	void PSMoveController::UpdateTrackingState() {
		assert(m_PSMServiceController != nullptr);
		assert(m_PSMServiceController->IsConnected);

		// The tracking status will be one of the following states:
		m_Pose.result = m_trackingStatus;

		m_Pose.deviceIsConnected = m_PSMServiceController->IsConnected;

		// These should always be false from any modern driver.  These are for Oculus DK1-like
		// rotation-only tracking.  Support for that has likely rotted in vrserver.
		m_Pose.willDriftInYaw = false;
		m_Pose.shouldApplyHeadModel = false;

		const PSMPSMove &view = m_PSMServiceController->ControllerState.PSMoveState;

		// No prediction since that's already handled in the psmove service
		m_Pose.poseTimeOffset = -0.016f;

		// No transform due to the current HMD orientation
		m_Pose.qDriverFromHeadRotation.w = 1.f;
		m_Pose.qDriverFromHeadRotation.x = 0.0f;
		m_Pose.qDriverFromHeadRotation.y = 0.0f;
		m_Pose.qDriverFromHeadRotation.z = 0.0f;
		m_Pose.vecDriverFromHeadTranslation[0] = 0.f;
		m_Pose.vecDriverFromHeadTranslation[1] = 0.f;
		m_Pose.vecDriverFromHeadTranslation[2] = 0.f;

		// Set position
		{
			const PSMVector3f &position = view.Pose.Position;
			m_Pose.vecPosition[0] = position.x * k_fScalePSMoveAPIToMeters;
			m_Pose.vecPosition[1] = position.y * k_fScalePSMoveAPIToMeters;
			m_Pose.vecPosition[2] = position.z * k_fScalePSMoveAPIToMeters;
		}

		// virtual extend controllers
		if (m_fVirtuallExtendControllersYMeters != 0.0f || m_fVirtuallExtendControllersZMeters != 0.0f) {
			const PSMQuatf &orientation = view.Pose.Orientation;

			PSMVector3f shift = { (float)m_Pose.vecPosition[0], (float)m_Pose.vecPosition[1], (float)m_Pose.vecPosition[2] };

			if (m_fVirtuallExtendControllersZMeters != 0.0f) {

				PSMVector3f local_forward = { 0, 0, -1 };
				PSMVector3f global_forward = PSM_QuatfRotateVector(&orientation, &local_forward);

				shift = PSM_Vector3fScaleAndAdd(&global_forward, m_fVirtuallExtendControllersZMeters, &shift);
			}

			if (m_fVirtuallExtendControllersYMeters != 0.0f) {

				PSMVector3f local_forward = { 0, -1, 0 };
				PSMVector3f global_forward = PSM_QuatfRotateVector(&orientation, &local_forward);

				shift = PSM_Vector3fScaleAndAdd(&global_forward, m_fVirtuallExtendControllersYMeters, &shift);
			}

			m_Pose.vecPosition[0] = shift.x;
			m_Pose.vecPosition[1] = shift.y;
			m_Pose.vecPosition[2] = shift.z;
		}

		// Set rotational coordinates
		{
			const PSMQuatf &orientation = view.Pose.Orientation;

			m_Pose.qRotation.w = m_fVirtuallyRotateController ? -orientation.w : orientation.w;
			m_Pose.qRotation.x = orientation.x;
			m_Pose.qRotation.y = orientation.y;
			m_Pose.qRotation.z = m_fVirtuallyRotateController ? -orientation.z : orientation.z;
		}

		// Set the physics state of the controller
		/*{
			const PSMPhysicsData &physicsData = view.PhysicsData;

			m_Pose.vecVelocity[0] = physicsData.LinearVelocityCmPerSec.x
				* abs(pow(abs(physicsData.LinearVelocityCmPerSec.x), m_fLinearVelocityExponent))
				* k_fScalePSMoveAPIToMeters * m_fLinearVelocityMultiplier;
			m_Pose.vecVelocity[1] = physicsData.LinearVelocityCmPerSec.y
				* abs(pow(abs(physicsData.LinearVelocityCmPerSec.y), m_fLinearVelocityExponent))
				* k_fScalePSMoveAPIToMeters * m_fLinearVelocityMultiplier;
			m_Pose.vecVelocity[2] = physicsData.LinearVelocityCmPerSec.z
				* abs(pow(abs(physicsData.LinearVelocityCmPerSec.z), m_fLinearVelocityExponent))
				* k_fScalePSMoveAPIToMeters * m_fLinearVelocityMultiplier;

			m_Pose.vecAcceleration[0] = physicsData.LinearAccelerationCmPerSecSqr.x * k_fScalePSMoveAPIToMeters;
			m_Pose.vecAcceleration[1] = physicsData.LinearAccelerationCmPerSecSqr.y * k_fScalePSMoveAPIToMeters;
			m_Pose.vecAcceleration[2] = physicsData.LinearAccelerationCmPerSecSqr.z * k_fScalePSMoveAPIToMeters;

			m_Pose.vecAngularVelocity[0] = physicsData.AngularVelocityRadPerSec.x;
			m_Pose.vecAngularVelocity[1] = physicsData.AngularVelocityRadPerSec.y;
			m_Pose.vecAngularVelocity[2] = physicsData.AngularVelocityRadPerSec.z;

			m_Pose.vecAngularAcceleration[0] = physicsData.AngularAccelerationRadPerSecSqr.x;
			m_Pose.vecAngularAcceleration[1] = physicsData.AngularAccelerationRadPerSecSqr.y;
			m_Pose.vecAngularAcceleration[2] = physicsData.AngularAccelerationRadPerSecSqr.z;
		}*/

		m_Pose.poseIsValid =
			m_PSMServiceController->ControllerState.PSMoveState.bIsPositionValid &&
			m_PSMServiceController->ControllerState.PSMoveState.bIsOrientationValid;

		// This call posts this pose to shared memory, where all clients will have access to it the next
		// moment they want to predict a pose.
		vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unSteamVRTrackedDeviceId, m_Pose, sizeof(vr::DriverPose_t));
	}

	void PSMoveController::SetPendingHapticVibration(const vr::VREvent_HapticVibration_t &hapticData) {

		if (IsHapticIDForHapticData(k_PSMHapticID_Rumble, hapticData))
		{
			m_pendingHapticDurationSecs = hapticData.fDurationSeconds;
			m_pendingHapticAmplitude = hapticData.fAmplitude;
			m_pendingHapticFrequency = hapticData.fFrequency;
		}
	}

	// TODO - Make use of amplitude and frequency for Buffered Haptics, will give us patterning and panning vibration (for ds4?).
	// See: https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-input-touch-haptic/
	void PSMoveController::UpdateRumbleState() {
		if (!m_bRumbleSuppressed) {

			// pulse duration - the length of each pulse
			// amplitude - strength of vibration
			// frequency - speed of each pulse


			// convert to microseconds, the max duration received from OpenVR appears to be 5 micro seconds
			uint16_t pendingHapticPulseDurationMicroSecs = static_cast<uint16_t>(m_pendingHapticDurationSecs * 1000000);

			const float k_max_rumble_update_rate = 33.f; // Don't bother trying to update the rumble faster than 30fps (33ms)
			const float k_max_pulse_microseconds = 5000.f; // Docs suggest max pulse duration of 5ms, but we'll call 1ms max

			std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();
			bool bTimoutElapsed = true;

			if (m_lastTimeRumbleSentValid) {
				std::chrono::duration<double, std::milli> timeSinceLastSend = now - m_lastTimeRumbleSent;

				bTimoutElapsed = timeSinceLastSend.count() >= k_max_rumble_update_rate;
			}

			// See if a rumble request hasn't come too recently
			if (bTimoutElapsed) {
				float rumble_fraction = (static_cast<float>(pendingHapticPulseDurationMicroSecs) / k_max_pulse_microseconds) * m_pendingHapticAmplitude;

				if (rumble_fraction > 0)
					steamvrbridge::Logger::Debug("PSMoveController::UpdateRumble: m_pendingHapticPulseDurationSecs=%f ,pendingHapticPulseDurationMicroSecs=%d, rumble_fraction=%f\n", m_pendingHapticDurationSecs, pendingHapticPulseDurationMicroSecs, rumble_fraction);

				// Unless a zero rumble intensity was explicitly set, 
				// don't rumble less than 35% (no enough to feel)
				if (m_pendingHapticDurationSecs != 0) {
					if (rumble_fraction < 0.35f) {
						// rumble values less 35% isn't noticeable
						rumble_fraction = 0.35f;
					}
				}

				// Keep the pulse intensity within reasonable bounds
				if (rumble_fraction > 1.f) {
					rumble_fraction = 1.f;
				}

				// Actually send the rumble to the server
				PSM_SetControllerRumble(m_PSMServiceController->ControllerID, PSMControllerRumbleChannel_All, rumble_fraction);

				// Remember the last rumble we went and when we sent it
				m_lastTimeRumbleSent = now;
				m_lastTimeRumbleSentValid = true;

				// Reset the pending haptic pulse duration.
				// If another call to TriggerHapticPulse() is made later, it will stomp this value.
				// If no future haptic event is received by ServerDriver then the next call to UpdateRumbleState()
				// in k_max_rumble_update_rate milliseconds will set the rumble_fraction to 0.f
				// This effectively makes the shortest rumble pulse k_max_rumble_update_rate milliseconds.
				m_pendingHapticDurationSecs = DEFAULT_HAPTIC_DURATION;
				m_pendingHapticAmplitude = DEFAULT_HAPTIC_AMPLITUDE;
				m_pendingHapticFrequency = DEFAULT_HAPTIC_FREQUENCY;
			}
		} else {
			// Reset the pending haptic pulse duration since rumble is suppressed.
			m_pendingHapticDurationSecs = DEFAULT_HAPTIC_DURATION;
			m_pendingHapticAmplitude = DEFAULT_HAPTIC_AMPLITUDE;
			m_pendingHapticFrequency = DEFAULT_HAPTIC_FREQUENCY;
		}
	}

	void PSMoveController::UpdateBatteryChargeState(
		PSMBatteryState newBatteryEnum) {
		bool bIsBatteryCharging = false;
		float fBatteryChargeFraction = 0.f;

		switch (newBatteryEnum) {
			case PSMBattery_0:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 0.f;
				break;
			case PSMBattery_20:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 0.2f;
				break;
			case PSMBattery_40:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 0.4f;
				break;
			case PSMBattery_60:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 0.6f;
				break;
			case PSMBattery_80:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 0.8f;
				break;
			case PSMBattery_100:
				bIsBatteryCharging = false;
				fBatteryChargeFraction = 1.f;
				break;
			case PSMBattery_Charging:
				bIsBatteryCharging = true;
				fBatteryChargeFraction = 0.99f; // Don't really know the charge amount in this case
				break;
			case PSMBattery_Charged:
				bIsBatteryCharging = true;
				fBatteryChargeFraction = 1.f;
				break;
		}

		if (bIsBatteryCharging != m_bIsBatteryCharging) {
			m_bIsBatteryCharging = bIsBatteryCharging;
			vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, vr::Prop_DeviceIsCharging_Bool, m_bIsBatteryCharging);
		}

		if (fBatteryChargeFraction != m_fBatteryChargeFraction) {
			m_fBatteryChargeFraction = fBatteryChargeFraction;
			vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_DeviceBatteryPercentage_Float, m_fBatteryChargeFraction);
		}
	}

	void PSMoveController::Update() {
		Controller::Update();

		if (IsActivated() && m_PSMServiceController->IsConnected) {
			int seq_num = m_PSMServiceController->OutputSequenceNum;

			// Only other updating incoming state if it actually changed and is due for one
			if (m_nPoseSequenceNumber < seq_num) {
				m_nPoseSequenceNumber = seq_num;

				UpdateTrackingState();
				UpdateControllerState();
			}

			// Update the outgoing state
			UpdateRumbleState();
		}
	}

	void PSMoveController::RefreshWorldFromDriverPose() {
		TrackableDevice::RefreshWorldFromDriverPose();

		// Mark the calibration process as done once we have setup the world from driver pose
		m_trackingStatus = vr::TrackingResult_Running_OK;
	}
}
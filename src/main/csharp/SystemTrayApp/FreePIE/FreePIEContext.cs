﻿using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Timers;
using PSMoveService;

namespace SystemTrayApp
{
    public class FreePIEContext : SynchronizedContext
    {
        private static readonly Lazy<FreePIEContext> _lazyInstance = new Lazy<FreePIEContext>(() => new FreePIEContext());
        public static FreePIEContext Instance { get { return _lazyInstance.Value; } }

        private class FreePIESlotState
        {
            private FreePIEApi.FreepieData _slotData;
            public FreePIEApi.FreepieData SlotData
            {
                get { return _slotData; }
            }

            public FreePIESlotState()
            {
                _slotData = new FreePIEApi.FreepieData();
            }

            public void UpdateSlotData(FreePIESlotDefinition slotDefinition, PSMDevicePool psmDevicePool)
            {
                if (slotDefinition is FreePIEControllerSlotDefinition)
                {
                    FreePIEControllerSlotDefinition controllerSlotDef = (FreePIEControllerSlotDefinition)slotDefinition;

                    _slotData.x = FetchControllerSlotData(controllerSlotDef.xProperty, psmDevicePool);
                    _slotData.y = FetchControllerSlotData(controllerSlotDef.yProperty, psmDevicePool);
                    _slotData.z = FetchControllerSlotData(controllerSlotDef.zProperty, psmDevicePool);
                    _slotData.pitch = FetchControllerSlotData(controllerSlotDef.pitchProperty, psmDevicePool);
                    _slotData.roll = FetchControllerSlotData(controllerSlotDef.rollProperty, psmDevicePool);
                    _slotData.yaw = FetchControllerSlotData(controllerSlotDef.yawProperty, psmDevicePool);
                }
                else if (slotDefinition is FreePIEHmdSlotDefinition) 
                {
                    FreePIEHmdSlotDefinition hmdSlotDef = (FreePIEHmdSlotDefinition)slotDefinition;

                    _slotData.x = FetchHmdSlotData(hmdSlotDef.xProperty, psmDevicePool);
                    _slotData.y = FetchHmdSlotData(hmdSlotDef.yProperty, psmDevicePool);
                    _slotData.z = FetchHmdSlotData(hmdSlotDef.zProperty, psmDevicePool);
                    _slotData.pitch = FetchHmdSlotData(hmdSlotDef.pitchProperty, psmDevicePool);
                    _slotData.roll = FetchHmdSlotData(hmdSlotDef.rollProperty, psmDevicePool);
                    _slotData.yaw = FetchHmdSlotData(hmdSlotDef.yawProperty, psmDevicePool);
                }
            }

            private float FetchControllerSlotData(FreePIEControllerProperty property, PSMDevicePool psmDevicePool)
            {
                switch(property.controllerPropertySource) {
                    case eControllerPropertySource.POSITION_X:
                        return psmDevicePool.GetControllerPosition(property.controllerSource).x;
                    case eControllerPropertySource.POSITION_Y:
                        return psmDevicePool.GetControllerPosition(property.controllerSource).y;
                    case eControllerPropertySource.POSITION_Z:
                        return psmDevicePool.GetControllerPosition(property.controllerSource).z;
                    case eControllerPropertySource.ORIENTATION_ROLL:
                        return MathUtility.ExtractRoll(psmDevicePool.GetControllerOrientation(property.controllerSource));
                    case eControllerPropertySource.ORIENTATION_PITCH:
                        return MathUtility.ExtractPitch(psmDevicePool.GetControllerOrientation(property.controllerSource));
                    case eControllerPropertySource.ORIENTATION_YAW:
                        return MathUtility.ExtractYaw(psmDevicePool.GetControllerOrientation(property.controllerSource));
                    case eControllerPropertySource.ACCELEROMETER_X:
                        return psmDevicePool.GetControllerAccelerometer(property.controllerSource).x;
                    case eControllerPropertySource.ACCELEROMETER_Y:
                        return psmDevicePool.GetControllerAccelerometer(property.controllerSource).y;
                    case eControllerPropertySource.ACCELEROMETER_Z:
                        return psmDevicePool.GetControllerAccelerometer(property.controllerSource).z;
                    case eControllerPropertySource.GYROSCOPE_X:
                        return psmDevicePool.GetControllerGyroscope(property.controllerSource).x;
                    case eControllerPropertySource.GYROSCOPE_Y:
                        return psmDevicePool.GetControllerGyroscope(property.controllerSource).y;
                    case eControllerPropertySource.GYROSCOPE_Z:
                        return psmDevicePool.GetControllerGyroscope(property.controllerSource).z;
                    case eControllerPropertySource.MAGNETOMETER_X:
                        return psmDevicePool.GetControllerMagnetometer(property.controllerSource).x;
                    case eControllerPropertySource.MAGNETOMETER_Y:
                        return psmDevicePool.GetControllerMagnetometer(property.controllerSource).y;
                    case eControllerPropertySource.MAGNETOMETER_Z:
                        return psmDevicePool.GetControllerMagnetometer(property.controllerSource).z;
                    case eControllerPropertySource.BUTTONS:
                        return psmDevicePool.GetControllerButtonBitmaskAsFloat(property.controllerSource);
                    case eControllerPropertySource.TRIGGER:
                        return psmDevicePool.GetTriggerValue(property.controllerSource);
                    default:
                        break;

                }
                return 0;
            }

            private float FetchHmdSlotData(FreePIEHmdProperty property, PSMDevicePool _psmDevicePool)
            {
                switch (property.hmdPropertySource) {
                    case eHmdPropertySource.POSITION_X:
                        return _psmDevicePool.GetHmdPosition(property.hmdSource).x;
                    case eHmdPropertySource.POSITION_Y:
                        return _psmDevicePool.GetHmdPosition(property.hmdSource).y;
                    case eHmdPropertySource.POSITION_Z:
                        return _psmDevicePool.GetHmdPosition(property.hmdSource).z;
                    case eHmdPropertySource.ORIENTATION_ROLL:
                        return MathUtility.ExtractRoll(_psmDevicePool.GetHmdOrientation(property.hmdSource));
                    case eHmdPropertySource.ORIENTATION_PITCH:
                        return MathUtility.ExtractPitch(_psmDevicePool.GetHmdOrientation(property.hmdSource));
                    case eHmdPropertySource.ORIENTATION_YAW:
                        return MathUtility.ExtractYaw(_psmDevicePool.GetHmdOrientation(property.hmdSource));
                    case eHmdPropertySource.ACCELEROMETER_X:
                        return _psmDevicePool.GetHmdAccelerometer(property.hmdSource).x;
                    case eHmdPropertySource.ACCELEROMETER_Y:
                        return _psmDevicePool.GetHmdAccelerometer(property.hmdSource).y;
                    case eHmdPropertySource.ACCELEROMETER_Z:
                        return _psmDevicePool.GetHmdAccelerometer(property.hmdSource).z;
                    case eHmdPropertySource.GYROSCOPE_X:
                        return _psmDevicePool.GetHmdGyroscope(property.hmdSource).x;
                    case eHmdPropertySource.GYROSCOPE_Y:
                        return _psmDevicePool.GetHmdGyroscope(property.hmdSource).y;
                    case eHmdPropertySource.GYROSCOPE_Z:
                        return _psmDevicePool.GetHmdGyroscope(property.hmdSource).z;
                    default:
                        break;

                }
                return 0;
            }
        }

        private FreePIESlotDefinition[] _freePIESlotDefinitions;
        public FreePIESlotDefinition[] FreePIESlotDefinitions
        {
            get { return _freePIESlotDefinitions; }
        }

        private FreePIESlotState[] _freePIESlotStates;
        private FreePIEApi.FreepieData[] _freePIEOutput;
        private PSMDevicePool _psmDevicePool;

        private bool _bIsInitialized;
        public bool IsInitialized
        {
            get { return _bIsInitialized; }
        }

        public enum FreePIEConnectionState
        {
            disconnected,
            waitingForPSMoveService,
            connected,
            failed
        }

        private FreePIEConnectionState _connectionState;
        public FreePIEConnectionState ConnectionState
        {
            get { return _connectionState; }
        }

        public bool IsConnected
        {
            get { return _connectionState == FreePIEConnectionState.connected; }
        }

        private string _freePieRuntimePath;
        public string FreePieRuntimePath
        {
            get { return _freePieRuntimePath; }
        }

        private int _freePIEMaxSlotCount;
        public int FreePIEMaxSlotCount
        {
            get { return _freePIEMaxSlotCount; }
        }

        public delegate void ConnectedToFreePIE();
        public event ConnectedToFreePIE ConnectedToFreePIEEvent;

        public delegate void FreePIEConnectionFailure(string Reason);
        public event FreePIEConnectionFailure FreePIEConnectionFailureEvent;

        public delegate void DisconnectedFromFreePIE();
        public event DisconnectedFromFreePIE DisconnectedFromFreePIEEvent;

        private FreePIEContext()
        {
            _freePIESlotDefinitions = new FreePIESlotDefinition[0];
            _freePIESlotStates = new FreePIESlotState[0];
            _freePIEOutput = new FreePIEApi.FreepieData[0];
            _psmDevicePool = new PSMDevicePool();
            _connectionState = FreePIEConnectionState.disconnected;
            _bIsInitialized = false;
            _freePIEMaxSlotCount = 4;
        }

        public bool Init()
        {
            if (!_bIsInitialized)
            {
                _freePieRuntimePath = GetFreePieDLLPath();

                if (_freePieRuntimePath.Length > 0)
                {
                    // Start listening to PSMoveService device events
                    PSMoveServiceContext.Instance.ConnectedToPSMServiceEvent += OnConnectedToPSMServiceEvent;
                    PSMoveServiceContext.Instance.DisconnectedFromPSMServiceEvent += OnDisconnectedFromPSMServiceEvent;
                    PSMoveServiceContext.Instance.ControllerListUpdatedEvent += OnControllerListUpdatedEvent;
                    PSMoveServiceContext.Instance.HmdListUpdatedEvent += OnHmdListUpdatedEvent;
                    PSMoveServiceContext.Instance.PSMMessagesPolledEvent += OnPSMMessagesPolledEvent;

                    // Add the FreePIE DLL directory to the DLL search list
                    SetDllDirectory(_freePieRuntimePath);

                    // Get the max allowed slot count in free pie
                    try {
                        _freePIEMaxSlotCount = FreePIEApi.freepie_io_6dof_slots();
                        _bIsInitialized = true;
                    }
                    catch(Exception e)
                    {
                        Trace.TraceWarning(string.Format("Failed to access FreePIE DLL: {0}", e.Message));
                    }
                }
            }

            return _bIsInitialized;
        }

        public void Cleanup()
        {
            if (_bIsInitialized)
            {
                DisconnectFromFreePIE();

                // Stop listening to PSMoveService device events
                PSMoveServiceContext.Instance.ConnectedToPSMServiceEvent -= OnConnectedToPSMServiceEvent;
                PSMoveServiceContext.Instance.DisconnectedFromPSMServiceEvent -= OnDisconnectedFromPSMServiceEvent;
                PSMoveServiceContext.Instance.ControllerListUpdatedEvent -= OnControllerListUpdatedEvent;
                PSMoveServiceContext.Instance.HmdListUpdatedEvent -= OnHmdListUpdatedEvent;
                PSMoveServiceContext.Instance.PSMMessagesPolledEvent -= OnPSMMessagesPolledEvent;

                _bIsInitialized = false;
            }
        }

        public void ConnectToFreePIE(FreePIESlotDefinition[] newSlots)
        {
            if (_connectionState == FreePIEConnectionState.disconnected ||
                _connectionState == FreePIEConnectionState.failed)
            {
                InitSlotStates(newSlots);

                if (PSMoveServiceContext.Instance.IsConnected)
                {
                    OnConnectedToFreePIE();
                }
                else 
                {
                    if (PSMoveServiceContext.Instance.LaunchPSMoveServiceProcess())
                    {
                        _connectionState = FreePIEConnectionState.waitingForPSMoveService;
                    }
                    else 
                    {
                        _connectionState = FreePIEConnectionState.failed;
                        if (FreePIEConnectionFailureEvent != null)
                        {
                            FreePIEConnectionFailureEvent("Can't launch PSMoveService");
                        }
                    }
                }
            }
        }

        public void DisconnectFromFreePIE()
        {
            if (_connectionState == FreePIEConnectionState.connected)
            {
                _psmDevicePool.Cleanup();
                CleanupSlotStates();

                _connectionState = FreePIEConnectionState.disconnected;
                if (DisconnectedFromFreePIEEvent != null) {
                    DisconnectedFromFreePIEEvent();
                }
            }
        }

        private void OnConnectedToFreePIE()
        {
            _psmDevicePool.Init();

            _connectionState = FreePIEConnectionState.connected;
            if (ConnectedToFreePIEEvent != null)
            {
                ConnectedToFreePIEEvent();
            }
        }

        private void OnConnectedToPSMServiceEvent()
        {
            if (_connectionState == FreePIEConnectionState.waitingForPSMoveService)
            {
                OnConnectedToFreePIE();
            }
        }

        private void OnDisconnectedFromPSMServiceEvent()
        {
            if (_connectionState == FreePIEConnectionState.connected)
            {
                DisconnectFromFreePIE();
            }
        }

        private void OnHmdListUpdatedEvent()
        {
            if (_connectionState == FreePIEConnectionState.connected)
            {
                _psmDevicePool.RefreshHmdList();
                UpdateSlotStates();
                PublishSlotStates();
            }
        }

        private void OnControllerListUpdatedEvent()
        {
            if (_connectionState == FreePIEConnectionState.connected)
            {
                _psmDevicePool.RefreshControllerList();
                UpdateSlotStates();
                PublishSlotStates();
            }
        }

        private void OnPSMMessagesPolledEvent()
        {
            if (_connectionState == FreePIEConnectionState.connected)
            {
                UpdateSlotStates();
                PublishSlotStates();
            }
        }

        private void InitSlotStates(FreePIESlotDefinition[] newSlots)
        {
            if (newSlots.Length <= _freePIEMaxSlotCount)
            {
                _freePIESlotDefinitions = newSlots;
            }
            else 
            {
                _freePIESlotDefinitions = newSlots.Take(_freePIEMaxSlotCount).ToArray();
            }

            if (_freePIESlotStates.Length != _freePIESlotDefinitions.Length)
            {
                _freePIESlotStates = new FreePIESlotState[_freePIESlotDefinitions.Length];
                _freePIEOutput = new FreePIEApi.FreepieData[_freePIESlotDefinitions.Length];

                for (int slotIndex= 0; slotIndex < _freePIESlotDefinitions.Length; ++slotIndex)
                {
                    _freePIESlotStates[slotIndex] = new FreePIESlotState();
                }
            }
        }

        private void UpdateSlotStates()
        {
            for (int slotIndex = 0; slotIndex < _freePIESlotStates.Length; ++slotIndex)
            {
                FreePIESlotDefinition slotDefinition = _freePIESlotDefinitions[slotIndex];
                FreePIESlotState slotState = _freePIESlotStates[slotIndex];

                slotState.UpdateSlotData(slotDefinition, _psmDevicePool);
            }
        }

        private void PublishSlotStates()
        {
            // Copy the slot data to the final output array
            for (int slotIndex = 0; slotIndex < _freePIESlotDefinitions.Length; ++slotIndex)
            {
                _freePIEOutput[slotIndex] = _freePIESlotStates[slotIndex].SlotData;
            }

            int result = FreePIEApi.freepie_io_6dof_write(0, _freePIEOutput.Length, _freePIEOutput);
            if (result == FreePIEApi.FREEPIE_IO_ERROR_OUT_OF_BOUNDS) {
                Trace.TraceError("FreePIEContext - Could not write slots to freepie: OUT OF BOUNDS");
            }
            else if (result == FreePIEApi.FREEPIE_IO_ERROR_SHARED_DATA) {
                Trace.TraceError("FreePIEContext - Could not write slots to freepie: SHARED DATA ERROR");
            }
        }

        private void CleanupSlotStates()
        {
            _freePIESlotStates = new FreePIESlotState[0];
            _freePIEOutput = new FreePIEApi.FreepieData[0];
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetDllDirectory(string lpPathName);

        public static string GetFreePieDLLPath()
        {
            string dll_path = "";

            if (!Environment.Is64BitProcess && //TODO: 64-bit dll for FreePIE not included in FreePIE install at the moment
                RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                dll_path = 
                    Microsoft.Win32.Registry.GetValue(
                        string.Format("{0}\\Software\\{1}", Microsoft.Win32.Registry.CurrentUser, "FreePIE"),
                        "path", 
                        null) as string;
            }

            if (dll_path.Length == 0) {
                dll_path = SystemTrayApp.Program.GetExecutingDirectoryName();
            }

            return dll_path;
        }
    }
}

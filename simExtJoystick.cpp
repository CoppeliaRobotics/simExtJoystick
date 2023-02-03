#include <Windows.h>
#include <scriptFunctionData.h>
#include <simLib.h>
#include <iostream>
#include <dinput.h>
#include <shlwapi.h> // for the "PathRemoveFileSpec" function

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib, "Shlwapi.lib")

#define SIM_DLLEXPORT extern "C" __declspec(dllexport)

#define CONCAT(x,y,z) x y z
#define strConCat(x,y,z)    CONCAT(x,y,z)

volatile bool _joyThreadLaunched=false;
volatile bool _joyThreadEnded=false;
volatile bool _inJoyThread=false;
volatile bool joyGoodToRead=false;
LPDIRECTINPUT8 di;
LPDIRECTINPUTDEVICE8 joysticks[4]={NULL,NULL,NULL,NULL};
DIDEVCAPS capabilities[4];
DIJOYSTATE2 joystickStates[4];
int currentDeviceIndex=0;
int joystickCount=0;

LIBRARY simLib;

BOOL CALLBACK enumCallback(const DIDEVICEINSTANCE* instance, VOID* context)
{
    HRESULT hr;
    hr = di->CreateDevice(instance->guidInstance, &joysticks[currentDeviceIndex++], NULL);
    return DIENUM_CONTINUE;
}

BOOL CALLBACK enumAxesCallback(const DIDEVICEOBJECTINSTANCE* instance, VOID* context)
{
    HWND hDlg = (HWND)context;

    DIPROPRANGE propRange; 
    propRange.diph.dwSize       = sizeof(DIPROPRANGE); 
    propRange.diph.dwHeaderSize = sizeof(DIPROPHEADER); 
    propRange.diph.dwHow        = DIPH_BYID; 
    propRange.diph.dwObj        = instance->dwType;
    propRange.lMin              = -1000; 
    propRange.lMax              = +1000; 
    
    // Set the range for the axis
    if (FAILED(joysticks[currentDeviceIndex]->SetProperty(DIPROP_RANGE, &propRange.diph))) {
        return DIENUM_STOP;
    }

    return DIENUM_CONTINUE;
}

DWORD WINAPI _joyThread(LPVOID lpParam)
{
    _inJoyThread=true;
    _joyThreadLaunched=true;

    HRESULT hr;
    // Create a DirectInput device
    if (FAILED(hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, 
                                   IID_IDirectInput8, (VOID**)&di, NULL)))
    {
        simAddLog("Joystick", sim_verbosity_errors, "failed initializing DirectInput library.");
        _joyThreadEnded=true;
        _inJoyThread=false;
        return(0);
    }

    // Look for the first simple joystick we can find.
    if (FAILED(hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback,
                                NULL, DIEDFL_ATTACHEDONLY)))
    {
        simAddLog("Joystick", sim_verbosity_errors, "failed enumerating devices.");
        _joyThreadEnded=true;
        _inJoyThread=false;
        return(0);
    }

    // Make sure we got a joystick
    joystickCount=0;
    for (int i=0;i<4;i++)
    {
        if (joysticks[i]!=NULL)
            joystickCount++;
    }
    if (joystickCount==0) 
    { // joystick not found
        _joyThreadEnded=true;
        _inJoyThread=false;
        return(0);
    }

    // Set joystick properties:
    for (int i=0;i<4;i++)
    {
        if (joysticks[i]!=NULL)
        {
            if (FAILED(hr = joysticks[i]->SetDataFormat(&c_dfDIJoystick2))) 
                simAddLog("Joystick", sim_verbosity_errors, "failed at 'SetDataFormat'.");

            if (FAILED(hr = joysticks[i]->SetCooperativeLevel(NULL, DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
               ;// do not output an error here!      printf("Failed at 'SetCooperativeLevel'.\n");

            capabilities[i].dwSize = sizeof(DIDEVCAPS);
            if (FAILED(hr = joysticks[i]->GetCapabilities(&capabilities[i])))
                simAddLog("Joystick", sim_verbosity_errors, "failed at 'GetCapabilities'.");

            currentDeviceIndex=i;
            if (FAILED(hr = joysticks[i]->EnumObjects(enumAxesCallback, NULL, DIDFT_AXIS)))
                simAddLog("Joystick", sim_verbosity_errors, "failed at 'EnumObjects'.");
        }
    }
    joyGoodToRead=true;

    while (_joyThreadLaunched)
    {
        for (int i=0;i<4;i++)
        {
            if (joysticks[i]!=NULL)
            {
                hr = joysticks[i]->Poll();
                bool cont=true;
                if (FAILED(hr)) 
                {
                    hr = joysticks[i]->Acquire();
                    while (hr == DIERR_INPUTLOST)
                        hr = joysticks[i]->Acquire();

                    if ((hr == DIERR_INVALIDPARAM) || (hr == DIERR_NOTINITIALIZED))
                    {
                        simAddLog("Joystick", sim_verbosity_errors, "failed at fatal error.");
                        cont=false;
                    }

                    if (cont)
                    {
                        if (hr == DIERR_OTHERAPPHASPRIO)
                            cont=false;
                    }
                }
                if (cont)
                {
                    if (FAILED(hr = joysticks[i]->GetDeviceState(sizeof(DIJOYSTATE2), &joystickStates[i])))
                        simAddLog("Joystick", sim_verbosity_errors, "failed at 'GetDeviceState'.");
                }
            }
        }
        Sleep(2);
    }

    for (int i=0;i<4;i++)
    {
        if (joysticks[i]!=NULL)
            joysticks[i]->Unacquire();
        joysticks[i]=NULL;
    }

    _joyThreadEnded=true;
    _joyThreadLaunched=true;
    _inJoyThread=false;
    return(0);
}

void launchThreadIfNeeded()
{
    if (!_inJoyThread)
    {
        _joyThreadEnded=false;
        _joyThreadLaunched=false;
        joyGoodToRead=false;
        CreateThread(NULL,0,_joyThread,NULL,THREAD_PRIORITY_NORMAL,NULL);
        while (!_joyThreadLaunched)
            Sleep(2);
        while (_inJoyThread&&(!joyGoodToRead))
            Sleep(2);
    }
}


void killThreadIfNeeded()
{
    if (_inJoyThread)
    {
        _joyThreadLaunched=false;
        while (!_joyThreadLaunched)
            Sleep(2);
        _joyThreadLaunched=false;
        _joyThreadEnded=false;
    }
}

#define LUA_GETCOUNTOLD "simExtJoyGetCount"
#define LUA_GETCOUNT "simJoy.getCount"
const int inArgs_GETCOUNT[] = {
	0,
};

void LUA_GETCOUNT_CALLBACK(SScriptCallBack* cb)
{
	CScriptFunctionData D;
    launchThreadIfNeeded();
	D.pushOutData(CScriptFunctionDataItem(joystickCount));
	D.writeDataToStack(cb->stackID);
}

#define LUA_GETDATAOLD "simExtJoyGetData"
#define LUA_GETDATA "simJoy.getData"
const int inArgs_GETDATA[] = {
	1,
	sim_script_arg_int32,0,
};

void LUA_GETDATA_CALLBACK(SScriptCallBack* cb)
{
	CScriptFunctionData D;
	launchThreadIfNeeded();
	bool error = true;
	int index = 0;
	if (D.readDataFromStack(cb->stackID, inArgs_GETDATA, inArgs_GETDATA[0], LUA_GETDATA))
	{
		std::vector<CScriptFunctionDataItem>* inData = D.getInDataPtr();
		int ind = inData->at(0).int32Data[0];
        if ( (ind<joystickCount)&&(ind >=0) )
        { // Ok, there is a device at this index!
            index= ind;
            error=false;
        }
        else
            simSetLastError(LUA_GETDATA,"Invalid index."); // output an error
    }

    if (!error)
    {
		std::vector<int> axis1;
		axis1.push_back(joystickStates[index].lX);
		axis1.push_back(joystickStates[index].lY);
		axis1.push_back(joystickStates[index].lZ);
		D.pushOutData(CScriptFunctionDataItem(axis1));
		int buttons = 0;
		for (int i = 0; i<16; i++)
		{
			if (joystickStates[index].rgbButtons[i] != 0)
				buttons |= (1 << i);
		}
		D.pushOutData(CScriptFunctionDataItem(buttons));
		std::vector<int> rotAxis;
		rotAxis.push_back(joystickStates[index].lRx);
		rotAxis.push_back(joystickStates[index].lRy);
		rotAxis.push_back(joystickStates[index].lRz);
		D.pushOutData(CScriptFunctionDataItem(rotAxis));
		std::vector<int> sliders;
		sliders.push_back(joystickStates[index].rglSlider[0]);
		sliders.push_back(joystickStates[index].rglSlider[1]);
		D.pushOutData(CScriptFunctionDataItem(sliders));
		std::vector<int> povs;
		povs.push_back(joystickStates[index].rgdwPOV[0]);
		povs.push_back(joystickStates[index].rgdwPOV[1]);
		povs.push_back(joystickStates[index].rgdwPOV[2]);
		povs.push_back(joystickStates[index].rgdwPOV[3]);
		D.pushOutData(CScriptFunctionDataItem(povs));
    }
	D.writeDataToStack(cb->stackID);
}




SIM_DLLEXPORT unsigned char simStart(void* reservedPointer,int reservedInt)
{ // This is called just once, at the start of CoppeliaSim

    // Dynamically load and bind CoppeliaSim functions:
    char curDirAndFile[1024];
    GetModuleFileName(NULL,curDirAndFile,1023);
    PathRemoveFileSpec(curDirAndFile);
    std::string currentDirAndPath(curDirAndFile);
    std::string temp(currentDirAndPath);
    temp+="\\coppeliaSim.dll";
    simLib=loadSimLibrary(temp.c_str());
    if (simLib==NULL)
    {
        printf("simExtJoystick: error: could not find or correctly load the CoppeliaSim library. Cannot start the plugin.\n"); // cannot use simAddLog here.
        return(0); // Means error, CoppeliaSim will unload this plugin
    }
    if (getSimProcAddresses(simLib)==0)
    {
        printf("simExtJoystick: error: could not find all required functions in the CoppeliaSim library. Cannot start the plugin.\n"); // cannot use simAddLog here.
        unloadSimLibrary(simLib);
        return(0); // Means error, CoppeliaSim will unload this plugin
    }

	// Register the new functions:
	simRegisterScriptCallbackFunction(strConCat(LUA_GETCOUNT, "@", "Joystick"), strConCat("int count=", LUA_GETCOUNT, "()"), LUA_GETCOUNT_CALLBACK);
	simRegisterScriptCallbackFunction(strConCat(LUA_GETDATA, "@", "Joystick"), strConCat("int[3] axes, int buttons,int[3] rotAxes,int[3] slider,int[4] pov=", LUA_GETDATA, "(int deviceIndex)"), LUA_GETDATA_CALLBACK);
	
	// Following for backward compatibility:
	simRegisterScriptVariable(LUA_GETCOUNTOLD, LUA_GETCOUNT, -1);
	simRegisterScriptCallbackFunction(strConCat(LUA_GETCOUNTOLD, "@", "Joystick"), strConCat("Please use the ", LUA_GETCOUNT, " notation instead"), 0);
	simRegisterScriptVariable(LUA_GETDATAOLD, LUA_GETDATA, -1);
	simRegisterScriptCallbackFunction(strConCat(LUA_GETDATAOLD, "@", "Joystick"), strConCat("Please use the ", LUA_GETDATA, " notation instead"), 0);


    return(4);  // initialization went fine, return the version number of this plugin!
                // version 3 was for CoppeliaSim 4.3 or earlier
}

SIM_DLLEXPORT void simEnd()
{ // This is called just once, at the end of CoppeliaSim
    // Release resources here..
    killThreadIfNeeded();
    unloadSimLibrary(simLib); // release the library
}

SIM_DLLEXPORT void* simMessage(int message,int* auxiliaryData,void* customData,int* replyData)
{ // This is called quite often. Just watch out for messages/events you want to handle
    // This function should not generate any error messages:
    int errorModeSaved;
    simGetIntegerParameter(sim_intparam_error_report_mode,&errorModeSaved);
    simSetIntegerParameter(sim_intparam_error_report_mode,sim_api_errormessage_ignore);

    void* retVal=NULL;

    if (message==sim_message_eventcallback_instancepass)
    { // It is important to always correctly react to events in CoppeliaSim. This message is the most convenient way to do so:

        int flags=auxiliaryData[0];
        bool sceneContentChanged=((flags&(1+2+4+8+16+32+64+256))!=0); // object erased, created, model or scene loaded, und/redo called, instance switched, or object scaled since last sim_message_eventcallback_instancepass message 
        bool instanceSwitched=((flags&64)!=0);

        if (instanceSwitched)
        {

        }

        if (sceneContentChanged)
        { // we actualize plugin objects for changes in the scene

        }
    }

    // You can add more messages to handle here

    simSetIntegerParameter(sim_intparam_error_report_mode,errorModeSaved); // restore previous settings
    return(retVal);
}

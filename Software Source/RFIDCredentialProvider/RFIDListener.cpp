//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) 2010 Amal Graafstra. All rights reserved.
//

#include "RFIDListener.h"
#include <strsafe.h>
#include <atlstr.h>
#include <Windows.h>
#include <iostream>
#include <SetupAPI.h>

// length of RFID data in badge
const int RFID_Length = 50;	// maximum RFID message length
TCHAR RFID_Port[] = _T(""); //append COM port data to this string to set communications port (e.g. COM3)
TCHAR D_RFID_Port[] = _T("3");
TCHAR RFID_Lead[] = _T("ACK "); //preamble to look for to indicate an RFID tag ID is being sent
TCHAR RFID_Auth[] = _T("e2f3g"); //device unique id
TCHAR RFID_Term[] = _T("\r\n"); //post tag ID string

CString deviceID(_T("USB\\VID_1A86&PID_7523\\"));
CString instanceID;
CString portName = "";
DWORD requiredSize;

int value = 0;
int validate = 0;

CRFIDListener::CRFIDListener(void)
{
    _pProvider = NULL;
	_bQuit = FALSE;
	ZeroMemory(_UserName, sizeof(_UserName));
	ZeroMemory(_Password, sizeof(_Password));

	ReadSettings();
	ReadCredentials();
}

CRFIDListener::~CRFIDListener(void)
{
	// signal thread to die
	_bQuit = TRUE;
	// wait for it to go away, but no more than 10 seconds
	// this is a little overkill, since the thread should die in less than 2 seconds
	WaitForSingleObject(_hThread, 10000);

    // We'll also make sure to release any reference we have to the provider.
    if (_pProvider != NULL)
    {
        _pProvider->Release();
        _pProvider = NULL;
    }
}

// Read the credentials file C:\Windows\System32\RFIDCredentials.txt
// The file is formated with one set of credentials per line.
// The fields are separated by a pipe character '|'.
// Each set of credentials consists of three fields: 
//		RFID Tag ID
//		User Name
//		Password
// Domain logins are supported by prefixing the User Name field with Domain backslash:
//		Domain\User Name
// All the data is currently stored as clear text, and any use of this project in a 
// live environment should use some form of encryption to protect account credentials
void CRFIDListener::ReadCredentials()
{
	HANDLE hFile = CreateFile(_T("C:/Windows/System32/RFIDCredentials.txt"), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		// read the entire file into memory
		DWORD size = GetFileSize(hFile, NULL);
		char *pText = new char[size + (__int64)1];
		ZeroMemory(pText, size + (__int64)1);
		
		int vFile = ReadFile(hFile, pText, size, &size, NULL);

		if (vFile == 0)
		{
			validate = 0;
		}

		// TODO: If entire file is encrypted, this is where decryption should occur
		CString text = pText;
		delete []pText;

		// parse text file by lines
		CString sLine;
		CString paswd;
		int line = 0;
		for (sLine = text.Tokenize(_T("\r\n"), line); line != -1; sLine = text.Tokenize(_T("\r\n"), line))
		{
			// add each credential, parsing the line by pipe characters '|'
			int nIndex = Credentials.Add();
			int field = 0;
			Credentials[nIndex].sID = sLine.Tokenize(_T("|"), field);
			Credentials[nIndex].sUserName = sLine.Tokenize(_T("|"), field);
			Credentials[nIndex].sPassword = sLine.Tokenize(_T("|"), field);
			//paswd = Credentials[nIndex].sPassword;
		}
	}
}

// Read the settings file C:\Windows\System32\RFIDCredSettings.txt
// The file is formated with one setting per line.
// The fields are separated by an = character.
// Expected settings are: 
//		COM=3
//		LEAD=
//		AUTH=
//		TERM=
void CRFIDListener::ReadSettings()
{
	HANDLE hFile = CreateFile(_T("C:/Windows/System32/RFIDCredSettings.txt"), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	if (hFile != INVALID_HANDLE_VALUE)
	{
		// read the entire file into memory
		DWORD size = GetFileSize(hFile, NULL);
		char *pText = new char[size+ (__int64)1];
		ZeroMemory(pText, size + (__int64)1);

		int vFile = ReadFile(hFile, pText, size, &size, NULL);

		if (vFile == 0)
		{
			validate = 0;
		}
		
		CloseHandle(hFile);
		CString text = pText;
		delete []pText;

		// parse text file by lines
		CString sLine;
		int line = 0;
		for (sLine = text.Tokenize(_T("\r\n"), line); line != -1; sLine = text.Tokenize(_T("\r\n"), line))
		{
			// check lines and adjust settings
			if (sLine.Left(4)=="COM=") {_tcscpy(D_RFID_Port,sLine.Mid(4));}
			if (sLine.Left(5)=="LEAD=") {_tcscpy(RFID_Lead,sLine.Mid(5));}
			if (sLine.Left(5) == "AUTH=") { _tcscpy(RFID_Auth,sLine.Mid(5)); }
			if (sLine.Left(5)=="TERM=") {
				sLine.Replace(_T("\\r"),_T("\r")); // replace carriage return
				sLine.Replace(_T("\\n"),_T("\n")); // replace new line
				_tcscpy(RFID_Term,sLine.Mid(5));
			}
		}

		if (0)	// change (0) to (1) for debugging purposes
		{
			CString msg;
			msg = CString(_T("Port: ")) + RFID_Port + CString(_T("\r\nAuth ")) + RFID_Auth + CString(_T("\r\nTerm: ")) + RFID_Term;
			MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
		}
	}
}

// Performs the work required to spin off our thread so we can listen for serial data
HRESULT CRFIDListener::Initialize(CRFIDCredentialProvider *pProvider)
{
    HRESULT hr = S_OK;

    // Be sure to add a release any existing provider we might have, then add a reference
    // to the provider we're working with now.
    if (_pProvider != NULL)
    {
        _pProvider->Release();
    }
    _pProvider = pProvider;
    _pProvider->AddRef();
    
    // Create and launch the window thread.
    _hThread = ::CreateThread(NULL, 0, CRFIDListener::_ThreadProc, (LPVOID) this, 0, NULL);
    if (_hThread == NULL)
    {
        hr = HRESULT_FROM_WIN32(::GetLastError());
    }

    return hr;
}

// Wraps our internal connected status so callers can easily access it.
BOOL CRFIDListener::GetConnectedStatus()
{
	// we are connected if we have a valid user name
	return lstrlen(_UserName);
}

int CRFIDListener::WaitForSerialData()
{
	GUID classGuid[1];
	SP_DEVINFO_DATA devInfo;

	if (!SetupDiClassGuidsFromName(_T("PORTS"), classGuid, 1, &requiredSize))
		return value;

	// List up PORTS class devices in present
	HDEVINFO hDevInfoSet = SetupDiGetClassDevs(classGuid, NULL, NULL, DIGCF_PRESENT);

	if (hDevInfoSet == INVALID_HANDLE_VALUE)
		return value;
	// Enumerate devices in the list

	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD deviceIndex = 0; SetupDiEnumDeviceInfo(hDevInfoSet, deviceIndex, &devInfo); deviceIndex++)
	{
		//std::cout << "In for\n";
		// Get device instance ID
		SetupDiGetDeviceInstanceId(hDevInfoSet, &devInfo, NULL, 0, &requiredSize);
		SetupDiGetDeviceInstanceId(hDevInfoSet, &devInfo,
			instanceID.GetBuffer(requiredSize), requiredSize, NULL);
		instanceID.ReleaseBuffer();
		// Check device ID
		//std::cout << instanceID.Find(deviceID);

		if (instanceID.Find(deviceID) != 0)
			continue;
		// Split serial number
		//serialNumer = instanceID.Right(instanceID.GetLength() - deviceID.GetLength());
		// Open device parameters reg key
		HKEY hkey = SetupDiOpenDevRegKey(hDevInfoSet, &devInfo, DICS_FLAG_GLOBAL,
			0, DIREG_DEV, KEY_READ);

		if (hkey == INVALID_HANDLE_VALUE)
			continue;

		// Qurey for portname
		RegQueryValueEx(hkey, _T("PortName"), NULL, NULL, NULL, &requiredSize);
		RegQueryValueEx(hkey, _T("PortName"), NULL, NULL,
			(LPBYTE)portName.GetBuffer(requiredSize), &requiredSize);
		portName.ReleaseBuffer();
		// Close reg key
		RegCloseKey(hkey);
		// Print result
		//tprintf(_T("\nPort: %s\n"), portName);
	}
	//_tprintf(_T("\nPort Num: %s\n"), portName.Right(2));

	portName = portName.Mid(3, portName.GetLength());

	TCHAR* RFID_Port1 = new TCHAR[portName.GetLength() + (__int64)1];
	_tcscpy(RFID_Port1, portName);
	_tcscpy(RFID_Port, RFID_Port1);
	delete[] RFID_Port1;

	// Destroy device info list
	SetupDiDestroyDeviceInfoList(hDevInfoSet);

	// open the com port
	HANDLE hCom = CreateFile(CString(_T("\\\\.\\COM")) + RFID_Port, GENERIC_ALL, 0, NULL, OPEN_EXISTING, 0, NULL);

	/*HANDLE hCom = CreateFile(CString(_T("\\\\.\\COM")) + RFID_Port, //RFID_Port should only contain "COM3" or "COM2" etc.
								GENERIC_ALL,	 // we are reading & writing from the COM port
								0,               // exclusive access
								NULL,            // no security
								OPEN_EXISTING,
								0,               // no overlapped I/O
								NULL);           // null template */

	if (0)
	{
		CString msg;
		msg.Format(_T("Click on OK to login!"));
		MessageBox(NULL, msg, _T("Prompt"), MB_OK | MB_TOPMOST | MB_ICONASTERISK);
	}

	if (hCom == INVALID_HANDLE_VALUE)
	{
		return value;
	}

	// initialize port with the correct baud rate, etc.
	DCB  dcb;
	FillMemory( &dcb, sizeof(dcb), 0 );

	BOOL bResult = FALSE;

	if (GetCommState(hCom, &dcb))
	{
		dcb.BaudRate    = CBR_9600;
		dcb.fBinary     = TRUE;
		dcb.ByteSize    = 8;
		dcb.fParity     = FALSE;
		dcb.Parity		= NOPARITY;
		dcb.StopBits    = ONESTOPBIT;
		// TODO: flow control is currently disabled, this can be turned on
		// if the RFID reader uses it
		dcb.fDtrControl = DTR_CONTROL_DISABLE;	//DTR_CONTROL_ENABLE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;	//RTS_CONTROL_ENABLE;

		if (0)
		{
			CString msg;
			msg.Format(_T("In GetCommState"));
			MessageBox(NULL, msg, _T("Prompt"), MB_OK | MB_TOPMOST);
		}

		if (SetCommState(hCom, &dcb))
		{
			COMMTIMEOUTS CommTimeouts = {0};

			CommTimeouts.ReadIntervalTimeout         = 100000/9600;	// 10x intercharacter timing
			CommTimeouts.ReadTotalTimeoutConstant    = 1000;		// 1 second timeout overall
			CommTimeouts.ReadTotalTimeoutMultiplier  = 0;

			if (0)
			{
				CString msg;
				msg.Format(_T("In SetCommState"));
				MessageBox(NULL, msg, _T("Prompt"), MB_OK | MB_TOPMOST);
			}

			if (SetCommTimeouts(hCom, &CommTimeouts))
			{
				bResult = TRUE;
			}
			else
			{
				CString msg;
				msg.Format(_T("SetCommTimeouts failed with error = %d"), GetLastError());
				MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
			}
		}
		else
		{
			CString msg; 
			msg.Format(_T("SetCommState failed with error = %d"), GetLastError());
			MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
		}

	}
	else
	{
		if (0)
			MessageBox(NULL, _T("GetCommState failed"), _T("WaitForSerialData"), MB_OK | MB_TOPMOST);
	}

	if (bResult)
	{
		if (0)
		{
			CString msg;
			msg.Format(_T("In bResult"));
			MessageBox(NULL, msg, _T("Prompt"), MB_OK | MB_TOPMOST);
		}
		// buffer for RFID data
		TCHAR _RFID[RFID_Length+1];
		ZeroMemory(_RFID, sizeof(_RFID));
		int nIndex = 0;	// current buffer index

		enum RFIDState
		{
			Lead_in,
			ID_Data,
		};

		char bytes_to_send[5];
		bytes_to_send[0] = RFID_Auth[0];
		bytes_to_send[1] = RFID_Auth[1];
		bytes_to_send[2] = RFID_Auth[2];
		bytes_to_send[3] = RFID_Auth[3];
		bytes_to_send[4] = RFID_Auth[4];

		RFIDState state = Lead_in;
		int Lead_Length = lstrlen(RFID_Lead);
		int Term_Length = lstrlen(RFID_Term);
		//int Auth_Length = lstrlen(RFID_Auth);

		while (!_bQuit)	// _bQuit is set when we want the thread to die
		{
			
			// read data 1 byte at a time
			char buffer;
			DWORD nBytesRead, bytes_written;

			WriteFile(hCom, bytes_to_send, 5, &bytes_written, NULL);
			// send device id for authentication.

			if (0)	// change (0) to (1) for debugging purposes
			{
				CString msg;
				msg.Format(_T("RFID_Lead = %s\r\nRFID_Term = %s\r"),  RFID_Lead, RFID_Term);
				//msg.Format(_T("RFID_Auth[4] = %c\r\nAuth Length = %d\r"),RFID_Auth[4],Auth_Length);
				MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
			}

			int vFile = ReadFile(hCom, &buffer, 1, &nBytesRead, NULL);

			if (vFile == 0)
			{
				return value;
			}

			// did we read a byte?
			if (nBytesRead)
			{
				// add it to our buffer
				_RFID[nIndex++] = buffer;

				if (0)	// change (0) to (1) for debugging purposes
				{
					CString msg;
					msg.Format(_T("Reading COM data:\r\nstate = %d\r\nnIndex = %d\r\nRFID = %s"), state, nIndex, _RFID);
					MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
				}
				switch (state)
				{
				case Lead_in:
					if (_tcsncmp(_RFID, RFID_Lead, nIndex))
					{
						// mismatch, lead-in not found
						// if we have read in more than 1 character, shift the buffer
						// down and try again since the lead character may have garbage
						// in front of it which may mask it
						while ((nIndex > 1) && _tcsncmp(_RFID, RFID_Lead, nIndex))
						{
							CopyMemory(&_RFID[0], &_RFID[1], nIndex--);
						}
						_RFID[nIndex--] = 0;	// ignore char
					}
					else if (nIndex == Lead_Length)
					{
						state = ID_Data;
						// started reading a new badge, so clear current user data
						ZeroMemory(_UserName, sizeof(_UserName));
						ZeroMemory(_Password, sizeof(_Password));
						ZeroMemory(_RFID, sizeof(_RFID));
						nIndex = 0;
					}
					break;

				case ID_Data:
					if (nIndex > Term_Length)
					{
						// found termination characters
						if (!_tcsncmp(&_RFID[nIndex-Term_Length], RFID_Term, Term_Length))
						{
							// remove termination characters
							_RFID[nIndex-Term_Length] = 0;

							TCHAR sUserData[200];
							ZeroMemory(sUserData, sizeof(sUserData));
							ZeroMemory(_UserName, sizeof(_UserName));
							ZeroMemory(_Password, sizeof(_Password));
							//state = Lead_in;

							// find ID in our credentials file
							for (size_t i = 0; i < Credentials.GetCount(); ++i)
							{
								// when found, set the user name and password
								// and signal the provider
								if (Credentials[i].sID == _RFID)
								{
									lstrcpy(_UserName, Credentials[i].sUserName);
									lstrcpy(_Password, Credentials[i].sPassword);
									_pProvider->OnConnectStatusChanged();
									validate = 1;
									break;
								}
							}
							// clear the buffer once we are done with it
							ZeroMemory(_RFID, sizeof(_RFID));
							nIndex = 0;
							state = Lead_in;
						}
						// if buffer limit has been reached, clear it and start over, something is wrong
						if (nIndex == RFID_Length-1)
						{
							// started reading a new badge, so clear current user data
							ZeroMemory(_UserName, sizeof(_UserName));
							ZeroMemory(_Password, sizeof(_Password));
							ZeroMemory(_RFID, sizeof(_RFID));
							nIndex = 0;
							state = Lead_in;
						}
					}
					break;
				}
			}
		}
	}
	else
	{
		if (0)
			MessageBox(NULL, _T("Failed to initialize port"), _T(__FUNCTION__), MB_OK | MB_TOPMOST);
	}

	CloseHandle(hCom);
	value = 1;

	if (0)	// change (0) to (1) for debugging purposes
	{
		CString msg;
		msg.Format(_T("At the END\r\nValue = %d\r\nValidate = %d\r"), value,validate);
		MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
	}

	return value;
}

// Our thread procedure which waits for data on the serial port
DWORD WINAPI CRFIDListener::_ThreadProc(LPVOID lpParameter)
{
    CRFIDListener *pCommandWindow = static_cast<CRFIDListener *>(lpParameter);
    if (pCommandWindow == NULL)
    {
        // TODO: What's the best way to raise this error? This is a programming error
		// and should never actually ever occur.
        return 0;
    }

	// go read the serial data
	pCommandWindow->WaitForSerialData();
	
	while (value != 1 || validate != 1)
	{
		if (value == 0 || validate == 0)	// change (0) to (1) for debugging purposes
		{
			CString msg;
			msg.Format(_T("SmartKey Removed.\nPlease connect the SmartKey to login!"));
			MessageBox(NULL, msg, _T("Error"), MB_OK | MB_TOPMOST | MB_ICONHAND);
			pCommandWindow->WaitForSerialData();
		}
		else
			break;
	}

	/*if (pCommandWindow->WaitForSerialData() == 0)	// change (0) to (1) for debugging purposes
	{
		CString msg;
		msg.Format(_T("WaitForSerialData = SUCCESS"));
		MessageBox(NULL, msg, _T(__FUNCTION__), MB_OK | MB_TOPMOST);
	}*/

	ShellExecute(NULL, _T("open"), _T("C:\\Users\\Toncy John\\source\\repos\\Logoff_Console\\Logoff_Console\\bin\\Debug\\Logoff_Console.exe"), NULL, NULL, SW_SHOWDEFAULT);
	return 0;
}

void CRFIDListener::Disconnect()
{
	MessageBox(NULL, _T("Disconnected"), _T(__FUNCTION__), MB_OK | MB_TOPMOST);
	ZeroMemory(_UserName, sizeof(_UserName));
	ZeroMemory(_Password, sizeof(_Password));
	_pProvider->OnConnectStatusChanged();
}

/*
 Copyright 2007 Don HO <don.h@free.fr>

 This file is part of GUP.

 GUP is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 GUP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with GUP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../ZipLib/ZipFile.h"
#include "../ZipLib/utils/stream_utils.h"

#include <stdint.h>
#include <sys/stat.h>
#include <windows.h>
#include <fstream>
#include <string>
#include <sstream>
#include <commctrl.h>
#include <shlobj_core.h>
#include "resource.h"
#include <shlwapi.h>
#include "xmlTools.h"
#include "sha-256.h"
#include "dpiManager.h"
#include "Common.h"
#include "verifySignedfile.h"

#define CURL_STATICLIB
#include "../curl/include/curl/curl.h"

#define GUP_LOG_FILENAME L"c:\\tmp\\winup.log"

#ifdef _DEBUG
#define WRITE_LOG(fn, suffix, log) writeLog(fn, suffix, log);
#else
#define WRITE_LOG(fn, suffix, log)
#endif


using namespace std;

typedef vector<wstring> ParamVector;

HINSTANCE g_hInst = nullptr;
HHOOK g_hMsgBoxHook = nullptr;
HWND g_hAppWnd = nullptr;

DPIManager dpiManager;
static HWND hProgressDlg = nullptr;
static HWND hProgressBar = nullptr;
static bool doAbort = false;
static bool stopDL = false;
static wstring msgBoxTitle;
static wstring abortOrNot;
static wstring proxySrv = L"0.0.0.0";
static long proxyPort = 0;
static wstring winGupUserAgent = L"WinGup/";
static wstring dlFileName;
static wstring appIconFile;
static wstring nsisSilentInstallParam;

wstring FLAG_NSIS_SILENT_INSTALL_PARAM = L"/closeRunningNpp /S /runNppAfterSilentInstall";


static constexpr wchar_t MSGID_UPDATEAVAILABLE[] = L"An update package is available, do you want to download and install it?";
static constexpr wchar_t MSGID_VERSIONCURRENT[] = L"Current version is   :";
static constexpr wchar_t MSGID_VERSIONNEW[] = L"Available version is :";
static constexpr wchar_t MSGID_DOWNLOADSTOPPED[] = L"Download is stopped by user. Update is aborted.";
static constexpr wchar_t MSGID_CLOSEAPP[] = L" is opened.\rUpdater will close it in order to process the installation.\rContinue?";
static constexpr wchar_t MSGID_ABORTORNOT[] = L"Do you want to abort update download?";
static constexpr wchar_t MSGID_UNZIPFAILED[] = L"Can't unzip:\nOperation not permitted or decompression failed";
static constexpr wchar_t MSGID_NODOWNLOADFOLDER[] = L"Can't find any folder for downloading.\rPlease check your environment variables\"%TMP%\", \"%TEMP%\" and \"%APPDATA%\"";

static constexpr wchar_t FLAG_OPTIONS[] = L"-options";
static constexpr wchar_t FLAG_VERBOSE[] = L"-verbose";
static constexpr wchar_t FLAG_HELP[] = L"--help";

static constexpr wchar_t FLAG_UUZIP[] = L"-unzipTo";
static constexpr wchar_t FLAG_CLEANUP[] = L"-clean";

static constexpr wchar_t FLAG_INFOURL[] = L"-infoUrl=";
static constexpr wchar_t FLAG_FORCEDOMAIN[] = L"-forceDomain=";

static constexpr wchar_t FLAG_CHKCERT_SIG[] = L"-chkCertSig=";
static constexpr wchar_t FLAG_CHKCERT_TRUSTCHAIN[] = L"-chkCertTrustChain";
static constexpr wchar_t FLAG_CHKCERT_REVOC[] = L"-chkCertRevoc";
static constexpr wchar_t FLAG_CHKCERT_NAME[] = L"-chkCertName=";
static constexpr wchar_t FLAG_CHKCERT_SUBJECT[] = L"-chkCertSubject=";
static constexpr wchar_t FLAG_CHKCERT_KEYID[] = L"-chkCertKeyId=";
static constexpr wchar_t FLAG_CHKCERT_AUTHORITYKEYID[] = L"-chkCertAuthorityKeyId=";
static constexpr wchar_t FLAG_CHKCERT_XML[] = L"-chkCert4InfoXML";
static constexpr wchar_t FLAG_CHKCERT_KEYID_XML[] = L"-chkCertKeyId4XML=";
static constexpr wchar_t FLAG_ERRLOGPATH[] = L"-errLogPath=";

static constexpr wchar_t MSGID_HELP[] =
L"Usage:\r\n\
\r\n\
gup --help\r\n\
gup -options\r\n\
\r\n\
    --help : Show this help message (and quit program).\r\n\
    -options : Show the proxy configuration dialog (and quit program).\r\n\
\r\n\
Update mode:\r\n\
\r\n\
gup [-verbose] [-vVERSION_VALUE] [-pCUSTOM_PARAM]\r\n\
\r\n\
    -v : Launch GUP with VERSION_VALUE.\r\n\
         VERSION_VALUE is the current version number of program to update.\r\n\
         If you pass the version number as the argument,\r\n\
         then the version set in the gup.xml will be overrided.\r\n\
    -p : Launch GUP with CUSTOM_PARAM.\r\n\
         CUSTOM_PARAM will pass to destination by using GET method\r\n\
         with argument name \"param\"\r\n\
    -verbose: Show error/warning message if any.\r\n\
\r\n\
Update mode:\r\n\
\r\n\
gup [-vVERSION_VALUE] [-infoUrl=URL] [-forceDomain=URL_PREFIX]\r\n\
\r\n\
    -infoUrl= : Use URL to override the value of \"InfoUr`\" tag in gup.xml.\r\n\
                URL is the url to gain update and/or download information.\r\n\
    -forceDomain= : Use URL_PREFIX to verify whether if the download link contain\r\n\
                    the domain prifix URL_PREFIX.If not, the download won't be processed.\r\n\
\r\n\
Update mode:\r\n\
\r\n\
gup [-vVERSION_VALUE] [-infoUrl=URL] [-chkCertSig=YES_NO] [-chkCertTrustChain]\r\n\
    [-chkCertRevoc] [-chkCertName=\"CERT_NAME\"] [-chkCertSubject=\"CERT_SUBNAME\"]\r\n\
    [-chkCertKeyId=CERT_KEYID] [-chkCertAuthorityKeyId=CERT_AUTHORITYKEYID]\r\n\
    [-errLogPath=\"YOUR\\ERR\\LOG\\PATH.LOG\"]\r\n\
\r\n\
    -chkCertSig= : Enable signature check on downloaded binary with \"-chkCertSig=yes\".\r\n\
                   Otherwise all the other \"-chkCert*\" options will be ignored.\r\n\
    -chkCertTrustChain : Enable signature chain of trust verification.\r\n\
    -chkCertRevoc : Enable the verification of certificate revocation state.\r\n\
    -chkCertName= : Verify certificate name (quotes allowed for white-spaces).\r\n\
    -chkCertSubject= : Verify subject name (quotes allowed for white-spaces).\r\n\
    -chkCertKeyId= : Verify certificate key identifier.\r\n\
    -chkCertAuthorityKeyId= : Verify certificate authority key identifier.\r\n\
    -errLogPath= : override the default error log path. The default value is:\r\n\
                   \"%LOCALAPPDATA%\\WinGUp\\log\\securityError.log\"\r\n\
\r\n\
Update mode:\r\n\
\r\n\
gup [-vVERSION_VALUE] [-infoUrl=URL] [-chkCertKeyId=CERT_KEYID]\r\n\
\r\n\
    -chkCert4InfoXML : Enable signature check for XML (XMLDsig) returned from server.\r\n\
                       Enable this when the server signs the XML; it ensure the XML\r\n\
                       has not been altered or hijacked.\r\n\
    -chkCertKeyId4XML= : Use the certificate Key ID for authentication. If ignored,\r\n\
                         only XML integrity is checked; authentication is not verified.\r\n\
\r\n\
Download & unzip mode:\r\n\
\r\n\
gup -clean FOLDER_TO_ACTION\r\n\
gup -unzipTo [-clean] FOLDER_TO_ACTION ZIP_URL\r\n\
\r\n\
    -clean : Delete all files in FOLDER_TO_ACTION.\r\n\
    -unzipTo : Download zip file from ZIP_URL then unzip it into FOLDER_TO_ACTION.\r\n\
    ZIP_URL : The URL to download zip file.\r\n\
    FOLDER_TO_ACTION : The folder where we clean or/and unzip to.\r\n\
	";

HFONT hCmdLineEditFont = nullptr;

INT_PTR CALLBACK helpDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM)
{
	switch (message)
	{
		case WM_INITDIALOG:
		{
			// Center dialog on screen
			RECT rc;
			GetWindowRect(hDlg, &rc);
			int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
			int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
			SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

			SetDlgItemText(hDlg, IDC_COMMANDLINEARGS_EDIT, MSGID_HELP);

			// Create DPI-aware monospace font
			NONCLIENTMETRICS ncm {};
			ncm.cbSize = sizeof(NONCLIENTMETRICS);
			SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

			// Use the system font height but change to monospace
			hCmdLineEditFont = CreateFont(
				ncm.lfMessageFont.lfHeight,  // DPI-aware height from system
				0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
				L"Lucida Console");

			if (hCmdLineEditFont)
				SendDlgItemMessage(hDlg, IDC_COMMANDLINEARGS_EDIT, WM_SETFONT, (WPARAM)hCmdLineEditFont, TRUE);
		}
		break;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			{
				EndDialog(hDlg, IDOK);
				return TRUE;
			}
			break;

		case WM_DESTROY:
			if (hCmdLineEditFont)
			{
				DeleteObject(hCmdLineEditFont);
				hCmdLineEditFont = nullptr;
			}
			break;
	}
	return FALSE;
}

class DlgIconHelper
{
public:
	DlgIconHelper()
	{
		g_hMsgBoxHook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, NULL, GetCurrentThreadId());
	}

	~DlgIconHelper()
	{
		UnhookWindowsHookEx(g_hMsgBoxHook);
	}

	static LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode == HC_ACTION)
		{
			CWPSTRUCT* pcwp = (CWPSTRUCT*)lParam;

			if (pcwp->message == WM_INITDIALOG)
			{
				setIcon(pcwp->hwnd, appIconFile);
			}
		}

		return CallNextHookEx(g_hMsgBoxHook, nCode, wParam, lParam);
	}

	static void setIcon(HWND hwnd, const wstring& iconFile)
	{
		if (!iconFile.empty())
		{
			HICON hIcon = nullptr, hIconSm = nullptr;

			hIcon = reinterpret_cast<HICON>(LoadImage(NULL, iconFile.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
			hIconSm = reinterpret_cast<HICON>(LoadImage(NULL, iconFile.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
			if (hIcon && hIconSm)
			{
				SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
				SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
			}
		}
	}
};
DlgIconHelper dlgIconHelper;




void parseCommandLine(const wchar_t* commandLine, ParamVector& paramVector)
{
	if (!commandLine)
		return;

	wchar_t* cmdLine = new wchar_t[lstrlen(commandLine) + 1];
	lstrcpy(cmdLine, commandLine);

	wchar_t* cmdLinePtr = cmdLine;

	bool isBetweenFileNameQuotes = false;
	bool isStringInArg = false;
	bool isInWhiteSpace = true;

	int zArg = 0; // for "-z" argument: Causes Notepad++ to ignore the next command line argument (a single word, or a phrase in quotes).
	// The only intended and supported use for this option is for the Notepad Replacement syntax.

	bool shouldBeTerminated = false; // If "-z" argument has been found, zArg value will be increased from 0 to 1.
	// then after processing next argument of "-z", zArg value will be increased from 1 to 2.
	// when zArg == 2 shouldBeTerminated will be set to true - it will trigger the treatment which consider the rest as a argument, with or without white space(s).

	size_t commandLength = lstrlen(cmdLinePtr);
	std::vector<wchar_t*> args;
	for (size_t i = 0; i < commandLength && !shouldBeTerminated; ++i)
	{
		switch (cmdLinePtr[i])
		{
		case '\"': //quoted filename, ignore any following whitespace
		{
			if (!isStringInArg && !isBetweenFileNameQuotes && i > 0 && cmdLinePtr[i - 1] == '=')
			{
				isStringInArg = true;
			}
			else if (isStringInArg)
			{
				isStringInArg = false;
			}
			else if (!isBetweenFileNameQuotes)	//" will always be treated as start or end of param, in case the user forgot to add an space
			{
				args.push_back(cmdLinePtr + i + 1);	//add next param(since zero terminated original, no overflow of +1)
				isBetweenFileNameQuotes = true;
				cmdLinePtr[i] = 0;

				if (zArg == 1)
				{
					++zArg; // zArg == 2
				}
			}
			else //if (isBetweenFileNameQuotes)
			{
				isBetweenFileNameQuotes = false;
				//because we don't want to leave in any quotes in the filename, remove them now (with zero terminator)
				cmdLinePtr[i] = 0;
			}
			isInWhiteSpace = false;
		}
		break;

		case '\t': //also treat tab as whitespace
		case ' ':
		{
			isInWhiteSpace = true;
			if (!isBetweenFileNameQuotes && !isStringInArg)
			{
				cmdLinePtr[i] = 0;		//zap spaces into zero terminators, unless its part of a filename

				size_t argsLen = args.size();
				if (argsLen > 0 && lstrcmp(args[argsLen - 1], L"-z") == 0)
					++zArg; // "-z" argument is found: change zArg value from 0 (initial) to 1
			}
		}
		break;

		default: //default wchar_t, if beginning of word, add it
		{
			if (!isBetweenFileNameQuotes && !isStringInArg && isInWhiteSpace)
			{
				args.push_back(cmdLinePtr + i);	//add next param
				if (zArg == 2)
				{
					shouldBeTerminated = true; // stop the processing, and keep the rest string as it in the vector
				}

				isInWhiteSpace = false;
			}
		}
		}
	}
	paramVector.assign(args.begin(), args.end());
	delete[] cmdLine;
}

bool isInList(const wchar_t* token2Find, ParamVector & params)
{
	size_t nbItems = params.size();

	for (size_t i = 0; i < nbItems; ++i)
	{
		if (!lstrcmp(token2Find, params.at(i).c_str()))
		{
			params.erase(params.begin() + i);
			return true;
		}
	}
	return false;
}

bool getParamVal(wchar_t c, ParamVector & params, wstring & value)
{
	value = L"";
	size_t nbItems = params.size();

	for (size_t i = 0; i < nbItems; ++i)
	{
		const wchar_t* token = params.at(i).c_str();
		if (token[0] == '-' && lstrlen(token) >= 2 && token[1] == c) //dash, and enough chars
		{
			value = (token + 2);
			params.erase(params.begin() + i);
			return true;
		}
	}
	return false;
}

bool getParamValFromString(const wchar_t* str, ParamVector& params, std::wstring& value)
{
	value = L"";
	size_t nbItems = params.size();

	for (size_t i = 0; i < nbItems; ++i)
	{
		const wchar_t* token = params.at(i).c_str();
		std::wstring tokenStr = token;
		size_t pos = tokenStr.find(str);
		if (pos != std::wstring::npos && pos == 0)
		{
			value = (token + lstrlen(str));
			params.erase(params.begin() + i);
			return true;
		}
	}
	return false;
}

wstring PathAppend(wstring& strDest, const wstring& str2append)
{
	if (strDest.empty() && str2append.empty()) // "" + ""
	{
		strDest = L"\\";
		return strDest;
	}

	if (strDest.empty() && !str2append.empty()) // "" + titi
	{
		strDest = str2append;
		return strDest;
	}

	if (strDest[strDest.length() - 1] == '\\' && (!str2append.empty() && str2append[0] == '\\')) // toto\ + \titi
	{
		strDest.erase(strDest.length() - 1, 1);
		strDest += str2append;
		return strDest;
	}

	if ((strDest[strDest.length() - 1] == '\\' && (!str2append.empty() && str2append[0] != '\\')) // toto\ + titi
		|| (strDest[strDest.length() - 1] != '\\' && (!str2append.empty() && str2append[0] == '\\'))) // toto + \titi
	{
		strDest += str2append;
		return strDest;
	}

	// toto + titi
	strDest += L"\\";
	strDest += str2append;

	return strDest;
};

vector<wstring> tokenizeString(const wstring & tokenString, const char delim)
{
	//Vector is created on stack and copied on return
	std::vector<wstring> tokens;

	// Skip delimiters at beginning.
	string::size_type lastPos = tokenString.find_first_not_of(delim, 0);
	// Find first "non-delimiter".
	string::size_type pos = tokenString.find_first_of(delim, lastPos);

	while (pos != std::string::npos || lastPos != std::string::npos)
	{
		// Found a token, add it to the vector.
		tokens.push_back(tokenString.substr(lastPos, pos - lastPos));
		// Skip delimiters.  Note the "not_of"
		lastPos = tokenString.find_first_not_of(delim, pos);
		// Find next "non-delimiter"
		pos = tokenString.find_first_of(delim, lastPos);
	}
	return tokens;
};

bool deleteFileOrFolder(const wstring& f2delete)
{
	auto len = f2delete.length();
	wchar_t* actionFolder = new wchar_t[len + 2];
	lstrcpy(actionFolder, f2delete.c_str());
	actionFolder[len] = 0;
	actionFolder[len + 1] = 0;

	SHFILEOPSTRUCT fileOpStruct = { 0 };
	fileOpStruct.hwnd = NULL;
	fileOpStruct.pFrom = actionFolder;
	fileOpStruct.pTo = NULL;
	fileOpStruct.wFunc = FO_DELETE;
	fileOpStruct.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_ALLOWUNDO;
	fileOpStruct.fAnyOperationsAborted = false;
	fileOpStruct.hNameMappings = NULL;
	fileOpStruct.lpszProgressTitle = NULL;

	int res = SHFileOperation(&fileOpStruct);
	if (res != 0)
	{
		// beware that some of the SHFileOperation error codes are not the standard WIN32 ones
		// (e.g. 124 (0x7C) here means DE_INVALIDFILES and not the usual ERROR_INVALID_LEVEL)
		WRITE_LOG(GUP_LOG_FILENAME, L"deleteFileOrFolder, SHFileOperation failed with error code: ", std::to_wstring(res).c_str());
	}

	delete[] actionFolder;
	return (res == 0);
};


// unzipDestTo should be plugin home root + plugin folder name
// ex: %APPDATA%\..\local\Notepad++\plugins\myAwesomePlugin
bool decompress(const wstring& zipFullFilePath, const wstring& unzipDestTo)
{
	// if destination folder doesn't exist, create it.
	if (!::PathFileExists(unzipDestTo.c_str()))
	{
		if (!::CreateDirectory(unzipDestTo.c_str(), NULL))
			return false;
	}

	string zipFullFilePathA = ws2s(zipFullFilePath);
	ZipArchive::Ptr archive = ZipFile::Open(zipFullFilePathA.c_str());

	std::istream* decompressStream = nullptr;
	auto count = archive->GetEntriesCount();

	if (!count) // wrong archive format
		return false;

	for (size_t i = 0; i < count; ++i)
	{
		ZipArchiveEntry::Ptr entry = archive->GetEntry(static_cast<int>(i));
		assert(entry != nullptr);

		//("[+] Trying no pass...\n");
		decompressStream = entry->GetDecompressionStream();
		assert(decompressStream != nullptr);

		wstring file2extrait = s2ws(entry->GetFullName());
		wstring extraitFullFilePath = unzipDestTo;
		PathAppend(extraitFullFilePath, file2extrait);


		// file2extrait be separated into an array
		vector<wstring> strArray = tokenizeString(file2extrait, '/');
		wstring folderPath = unzipDestTo;

		if (entry->IsDirectory())
		{
			// if folder doesn't exist, create it.
			if (!::PathFileExists(extraitFullFilePath.c_str()))
			{
				const size_t msgLen = 1024;
				wchar_t msg[msgLen];
				swprintf(msg, msgLen, L"[+] Create folder '%s'\n", file2extrait.c_str());
				OutputDebugString(msg);		

				for (size_t k = 0; k < strArray.size(); ++k)
				{
					PathAppend(folderPath, strArray[k]);

					if (!::PathFileExists(folderPath.c_str()))
					{
						::CreateDirectory(folderPath.c_str(), NULL);
					}
					else if (!::PathIsDirectory(folderPath.c_str())) // The unzip core component is not reliable for the file/directory detection 
					{                                                 // Hence such hack to make the result is as correct as possible
						// if it's a file, remove it
						deleteFileOrFolder(folderPath);

						// create it
						::CreateDirectory(folderPath.c_str(), NULL);
					}
				}
			}
		}
		else // it's a file
		{
			const size_t msgLen = 1024;
			wchar_t msg[msgLen];
			swprintf(msg, msgLen, L"[+] Extracting file '%s'\n", file2extrait.c_str());
			OutputDebugString(msg);

			for (size_t k = 0; k < strArray.size() - 1; ++k) // loop on only directory, not on file (which is the last element)
			{
				PathAppend(folderPath, strArray[k]);
				if (!::PathFileExists(folderPath.c_str()))
				{
					::CreateDirectory(folderPath.c_str(), NULL);
				}
				else if (!::PathIsDirectory(folderPath.c_str())) // The unzip core component is not reliable for the file/directory detection 
				{                                                 // Hence such hack to make the result is as correct as possible
					// if it's a file, remove it
					deleteFileOrFolder(folderPath);

					// create it
					::CreateDirectory(folderPath.c_str(), NULL);
				}
			}

			std::ofstream destFile;
			destFile.open(extraitFullFilePath, std::ios::binary | std::ios::trunc);
			
			utils::stream::copy(*decompressStream, destFile);

			destFile.flush();
			destFile.close();
		}
	}

	// check installed dll
	wstring pluginFolder = PathFindFileName(unzipDestTo.c_str());
	wstring installedPluginPath = unzipDestTo + L"\\" + pluginFolder + L".dll";
	
	if (::PathFileExists(installedPluginPath.c_str()))
	{
		// DLL is deployed correctly.
		// OK and nothing to do.
	}
	else
	{
		// Remove installed plugin
		MessageBox(NULL, TEXT("The plugin package is built wrongly. This plugin will be uninstalled."), TEXT("GUP"), MB_OK | MB_APPLMODAL);

		deleteFileOrFolder(unzipDestTo);
		return FALSE;
	}

	return true;
};

static void goToScreenCenter(HWND hwnd)
{
    RECT screenRc;
	::SystemParametersInfo(SPI_GETWORKAREA, 0, &screenRc, 0);

    POINT center;
	center.x = screenRc.left + (screenRc.right - screenRc.left) / 2;
    center.y = screenRc.top + (screenRc.bottom - screenRc.top)/2;

	RECT rc;
	::GetWindowRect(hwnd, &rc);
	int x = center.x - (rc.right - rc.left)/2;
	int y = center.y - (rc.bottom - rc.top)/2;

	::SetWindowPos(hwnd, HWND_TOP, x, y, rc.right - rc.left, rc.bottom - rc.top, SWP_SHOWWINDOW);
};


// This is the getUpdateInfo call back function used by curl
static size_t getUpdateInfoCallback(char *data, size_t size, size_t nmemb, std::string *updateInfo)
{
	// What we will return
	size_t len = size * nmemb;
	
	// Is there anything in the buffer?
	if (updateInfo != NULL)
	{
		// Append the data to the buffer
		updateInfo->append(data, len);
	}

	return len;
}

static size_t getDownloadData(unsigned char *data, size_t size, size_t nmemb, FILE *fp)
{
	if (doAbort)
		return 0;

	size_t len = size * nmemb;
	fwrite(data, len, 1, fp);
	return len;
};

static size_t setProgress(HWND, double dlTotal, double dlSoFar, double, double)
{
	while (stopDL)
		::Sleep(1000);

	size_t downloadedRatio = SendMessage(hProgressBar, PBM_DELTAPOS, 0, 0);

	if (dlTotal != 0)
	{
		size_t step = size_t((dlSoFar * 100.0 / dlTotal) - downloadedRatio);

		SendMessage(hProgressBar, PBM_SETSTEP, (WPARAM)step, 0);
		SendMessage(hProgressBar, PBM_STEPIT, 0, 0);
	}

	const size_t percentageLen = 1024;
	wchar_t percentage[percentageLen];
	swprintf(percentage, percentageLen, L"Downloading %s: %Iu %%", dlFileName.c_str(), downloadedRatio);
	::SetWindowText(hProgressDlg, percentage);

	if (downloadedRatio == 100)
		SendMessage(hProgressDlg, WM_COMMAND, IDOK, 0);

	return 0;
};

LRESULT CALLBACK progressBarDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM )
{
	INITCOMMONCONTROLSEX InitCtrlEx;

	InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	InitCtrlEx.dwICC  = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&InitCtrlEx);

	switch(Msg)
	{
		case WM_INITDIALOG:
		{
			hProgressDlg = hWndDlg;
			int x = dpiManager.scaleX(20);
			int y = dpiManager.scaleY(20);
			int width = dpiManager.scaleX(280);
			int height = dpiManager.scaleY(17);
			hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE,
				x, y, width, height,
				hWndDlg, NULL, g_hInst, NULL);
			SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
			SendMessage(hProgressBar, PBM_SETSTEP, 1, 0);
			DlgIconHelper::setIcon(hProgressDlg, appIconFile);
			goToScreenCenter(hWndDlg);
			return TRUE;
		}

		case WM_COMMAND:
			switch(wParam)
			{
				case IDOK:
					EndDialog(hWndDlg, 0);
					return TRUE;

				case IDCANCEL:
					stopDL = true;
					if (abortOrNot == L"")
						abortOrNot = MSGID_ABORTORNOT;
					int abortAnswer = ::MessageBox(hWndDlg, abortOrNot.c_str(), msgBoxTitle.c_str(), MB_YESNO);
					if (abortAnswer == IDYES)
					{
						doAbort = true;
						EndDialog(hWndDlg, 0);
					}
					stopDL = false;
					return TRUE;
			}
			break;
	}

	return FALSE;
}

struct UpdateAvailableDlgStrings
{
	wstring _title;
	wstring _message;
	wstring _customButton;
	wstring _yesButton;
	wstring _yesSilentButton;
	wstring _noButton;
};

LRESULT CALLBACK yesNoNeverDlgProc(HWND hWndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
		{
			if (lParam)
			{
				UpdateAvailableDlgStrings* pUaDlgStrs = reinterpret_cast<UpdateAvailableDlgStrings*>(lParam);

				if (!(pUaDlgStrs->_message).empty())
					::SetDlgItemText(hWndDlg, IDC_YESNONEVERMSG, pUaDlgStrs->_message.c_str());
				if (!(pUaDlgStrs->_title).empty())
					::SetWindowText(hWndDlg, pUaDlgStrs->_title.c_str());

				if (!pUaDlgStrs->_customButton.empty())
					::SetDlgItemText(hWndDlg, IDCANCEL, pUaDlgStrs->_customButton.c_str());
				if (!pUaDlgStrs->_yesButton.empty())
					::SetDlgItemText(hWndDlg, IDYES, pUaDlgStrs->_yesButton.c_str());
				if (!pUaDlgStrs->_yesSilentButton.empty())
					::SetDlgItemText(hWndDlg, IDOK, pUaDlgStrs->_yesSilentButton.c_str());
				if (!pUaDlgStrs->_noButton.empty())
					::SetDlgItemText(hWndDlg, IDNO, pUaDlgStrs->_noButton.c_str());

				if (!g_hAppWnd)
				{
					::EnableWindow(::GetDlgItem(hWndDlg, IDCANCEL), FALSE); // no app-wnd to sent the updates disabling message signal
					FLAG_NSIS_SILENT_INSTALL_PARAM = L"/closeRunningNpp /S";
				}
			}

			goToScreenCenter(hWndDlg);
			return TRUE;
		}

		case WM_COMMAND:
		{
			switch (wParam)
			{
				case IDOK:
				case IDYES:
				case IDNO:
				case IDCANCEL:
					EndDialog(hWndDlg, wParam);
					return TRUE;

				default:
					break;
			}
		}

		case WM_DESTROY:
		{
			return TRUE;
		}
	}
	return FALSE;
}

LRESULT CALLBACK proxyDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM)
{

	switch(Msg)
	{
		case WM_INITDIALOG:
			::SetDlgItemText(hWndDlg, IDC_PROXYSERVER_EDIT, proxySrv.c_str());
			::SetDlgItemInt(hWndDlg, IDC_PORT_EDIT, proxyPort, FALSE);
			goToScreenCenter(hWndDlg);
			return TRUE; 

		case WM_COMMAND:
			switch(wParam)
			{
				case IDOK:
				{
					wchar_t proxyServer[MAX_PATH];
					::GetDlgItemText(hWndDlg, IDC_PROXYSERVER_EDIT, proxyServer, MAX_PATH);
					proxySrv = proxyServer;
					proxyPort = ::GetDlgItemInt(hWndDlg, IDC_PORT_EDIT, NULL, FALSE);
					EndDialog(hWndDlg, 1);
					return TRUE;
				}
				case IDCANCEL:
					EndDialog(hWndDlg, 0);
					return TRUE;
			}
			break;
	}

	return FALSE;
}

struct UpdateCheckParams
{
	GupNativeLang& _nativeLang;
	GupParameters& _gupParams;
};

LRESULT CALLBACK updateCheckDlgProc(HWND hWndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
		{
			auto* params = reinterpret_cast<UpdateCheckParams*>(lParam);
			if (params)
			{
				const wstring& title = params->_gupParams.getMessageBoxTitle();
				if (!title.empty())
					::SetWindowText(hWndDlg, title.c_str());

				wstring dlStr = params->_nativeLang.getMessageString("MSGID_DOWNLOADPAGE");
				wstring moreInfoStr = params->_nativeLang.getMessageString("MSGID_MOREINFO");
				if (!dlStr.empty() && !moreInfoStr.empty())
				{
					wstring goToDlStr = params->_nativeLang.getMessageString("MSGID_GOTODOWNLOADPAGETEXT");
					if (!goToDlStr.empty())
					{
						wstring moreInfoLink = L"<a id=\"id_moreinfo\">";
						moreInfoLink += moreInfoStr;
						moreInfoLink += L"</a>";
						wstring textWithLink = stringReplace(goToDlStr, L"$MSGID_MOREINFO$", moreInfoLink);

						wstring dlLink = L"<a id=\"id_download\">";
						dlLink += dlStr;
						dlLink += L"</a>";
						textWithLink = stringReplace(textWithLink, L"$MSGID_DOWNLOADPAGE$", dlLink);

						::SetDlgItemText(hWndDlg, IDC_DOWNLOAD_LINK, textWithLink.c_str());
					}
				}
			}
			goToScreenCenter(hWndDlg);
			return TRUE;
		}

		case WM_COMMAND:
		{
			switch LOWORD((wParam))
			{
				case IDOK:
				case IDYES:
				case IDNO:
				case IDCANCEL:
					EndDialog(hWndDlg, wParam);
					return TRUE;
				default:
					break;
			}
		}

		case WM_NOTIFY:
			switch (((LPNMHDR)lParam)->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					PNMLINK pNMLink = (PNMLINK)lParam;
					LITEM item = pNMLink->item;
					if (lstrcmpW(item.szID, L"id_download") == 0)
					{
						::ShellExecute(NULL, TEXT("open"), TEXT("https://notepad-plus-plus.org/downloads/"), NULL, NULL, SW_SHOWNORMAL);
						EndDialog(hWndDlg, wParam);
					}
					else if (lstrcmpW(item.szID, L"id_moreinfo") == 0)
					{
						::ShellExecute(NULL, TEXT("open"), TEXT("https://npp-user-manual.org/docs/upgrading/#new-version-available-but-auto-updater-find-nothing"), NULL, NULL, SW_SHOWNORMAL);
						EndDialog(hWndDlg, wParam);
					}
					break;
				}
			}
			break;
	}
	return FALSE;
}

static DWORD WINAPI launchProgressBar(void *)
{
	::DialogBox(g_hInst, MAKEINTRESOURCE(IDD_PROGRESS_DLG), NULL, reinterpret_cast<DLGPROC>(progressBarDlgProc));
	return 0;
}

bool downloadBinary(const wstring& urlFrom, const wstring& destTo, const wstring& sha2HashToCheck, pair<wstring, int> proxyServerInfo, bool isSilentMode, const pair<wstring, wstring>& stoppedMessage)
{
	FILE* pFile = _wfopen(destTo.c_str(), L"wb");
	if (!pFile)
		return false;

	//  Download the install package from indicated location
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	CURLcode res = CURLE_FAILED_INIT;
	CURL* curl = curl_easy_init();

	::CreateThread(NULL, 0, launchProgressBar, NULL, 0, NULL);
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, ws2s(urlFrom).c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getDownloadData);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, pFile);

		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, setProgress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, hProgressBar);

		curl_easy_setopt(curl, CURLOPT_USERAGENT, ws2s(winGupUserAgent).c_str());
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

		if (!proxyServerInfo.first.empty() && proxyServerInfo.second != -1)
		{
			curl_easy_setopt(curl, CURLOPT_PROXY, ws2s(proxyServerInfo.first).c_str());
			curl_easy_setopt(curl, CURLOPT_PROXYPORT, proxyServerInfo.second);
			curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
		}

		res = curl_easy_perform(curl);

		curl_easy_cleanup(curl);
	}

	if (res != CURLE_OK)
	{
		if (!isSilentMode && doAbort == false)
		{
			std::wstring errMsg = L"A fail occurred while trying to download: \n" + urlFrom + L"\n\n" + s2ws(errorBuffer);
			::MessageBoxW(NULL, errMsg.c_str(), L"curl error", MB_OK | MB_SYSTEMMODAL); // MB_SYSTEMMODAL ... need the WS_EX_TOPMOST (due to the possible progress-dlg...)
		}

		if (doAbort)
		{
			::MessageBox(NULL, stoppedMessage.first.c_str(), stoppedMessage.second.c_str(), MB_OK);
		}
		doAbort = false;
		return false;
	}
	fflush(pFile);
	fclose(pFile);

	//
	// Check the hash if need
	//
	bool ok = true;
	if (!sha2HashToCheck.empty())
	{
		std::string content = getFileContentA(ws2s(destTo).c_str());
		if (content.empty())
		{
			// Remove installed plugin
			MessageBox(NULL, L"The plugin package is not found.", L"Plugin cannot be found", MB_OK | MB_APPLMODAL);
			ok = false;
		}
		else
		{
			char sha2hashStr[65] = { '\0' };
			uint8_t sha2hash[32];
			calc_sha_256(sha2hash, reinterpret_cast<const uint8_t*>(content.c_str()), content.length());

			for (size_t i = 0; i < 32; i++)
			{
				sprintf(sha2hashStr + i * 2, "%02x", sha2hash[i]);
			}
			wstring sha2HashToCheckLowerCase = sha2HashToCheck;
			std::transform(sha2HashToCheckLowerCase.begin(), sha2HashToCheckLowerCase.end(), sha2HashToCheckLowerCase.begin(),
				[](wchar_t c) { return static_cast<wchar_t>(::tolower(c)); });

			wstring sha2hashStrW = s2ws(sha2hashStr);
			if (sha2HashToCheckLowerCase != sha2hashStrW)
			{
				wstring pluginPackageName = ::PathFindFileName(destTo.c_str());
				wstring msg = L"The hash of plugin package \"";
				msg += pluginPackageName;
				msg += L"\" is not correct. Expected:\r";
				msg += sha2HashToCheckLowerCase;
				msg += L"\r<> Found:\r";
				msg += sha2hashStrW;
				msg += L"\rThis plugin won't be installed.";
				MessageBox(NULL, msg.c_str(), L"Plugin package hash mismatched", MB_OK | MB_APPLMODAL);
				ok = false;
			}
		}
	}

	if (!ok)
	{
		// Remove downloaded plugin package
		deleteFileOrFolder(destTo);
		return false;
	}

	return true;
}

bool getUpdateInfo(const string& info2get, const GupParameters& gupParams, const GupExtraOptions& proxyServer, const wstring& customParam, const wstring& version, const wstring& customInfoURL)
{
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };

	// Check on the web the availibility of update
	// Get the update package's location
	CURL *curl;
	CURLcode res = CURLE_FAILED_INIT;

	curl = curl_easy_init();
	if (curl)
	{
		wstring infoURL = customInfoURL.empty() ? gupParams.getInfoLocation() : customInfoURL;

		std::wstring urlComplete = infoURL + L"?version=";
		if (!version.empty())
			urlComplete += version;
		else
			urlComplete += gupParams.getCurrentVersion();

		if (!customParam.empty())
		{
			wstring customParamPost = L"&param=";
			customParamPost += customParam;
			urlComplete += customParamPost;
		}
		else if (!gupParams.getParam().empty())
		{
			wstring customParamPost = L"&param=";
			customParamPost += gupParams.getParam();
			urlComplete += customParamPost;
		}

		curl_easy_setopt(curl, CURLOPT_URL, ws2s(urlComplete).c_str());


		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getUpdateInfoCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &info2get);

		wstring ua = gupParams.getSoftwareName();

		winGupUserAgent += VERSION_VALUE;
		if (ua != L"")
		{
			ua += L"/";
			ua += version;
			ua += L" (";
			ua += winGupUserAgent;
			ua += L")";

			winGupUserAgent = ua;
		}

		curl_easy_setopt(curl, CURLOPT_USERAGENT, ws2s(winGupUserAgent).c_str());
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

		if (proxyServer.hasProxySettings())
		{
			curl_easy_setopt(curl, CURLOPT_PROXY, ws2s(proxyServer.getProxyServer()).c_str());
			curl_easy_setopt(curl, CURLOPT_PROXYPORT, proxyServer.getPort());
			curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
		}

		res = curl_easy_perform(curl);

		curl_easy_cleanup(curl);
	}

	if (res != CURLE_OK)
	{
		if (!gupParams.isSilentMode())
			::MessageBoxA(NULL, errorBuffer, "curl error", MB_OK);
		return false;
	}
	return true;
}

int runInstaller(const wstring& app2runPath, const wstring& binWindowsClassName, const wstring& closeMsg, const wstring& closeMsgTitle)
{
	if (!binWindowsClassName.empty())
	{
		HWND h = ::FindWindowEx(NULL, NULL, binWindowsClassName.c_str(), NULL);
		if (h)
		{
			int installAnswer = ::MessageBox(h, closeMsg.c_str(), closeMsgTitle.c_str(), MB_YESNO | MB_APPLMODAL);
			if (installAnswer == IDNO)
			{
				return (int)NO_ERROR;
			}
		}

		// kill all processes of the app needs to be updated.
		while (h)
		{
			::SendMessage(h, WM_CLOSE, 0, 0);
			h = ::FindWindowEx(NULL, NULL, binWindowsClassName.c_str(), NULL);
		}
	}

	// execute the installer
	intptr_t result = (intptr_t)::ShellExecute(NULL, L"open", app2runPath.c_str(), nsisSilentInstallParam.c_str(), L".", SW_SHOW);
	if (result <= 32) // There's a problem (Don't ask me why, ask Microsoft)
	{
		WRITE_LOG(GUP_LOG_FILENAME, L"runInstaller, ShellExecute failed with error code: ", std::to_wstring(result).c_str());
		return (int)result;
	}

	return (int)ERROR_SUCCESS;
}


std::wstring getSpecialFolderLocation(int folderKind)
{
	TCHAR path[MAX_PATH];
	const HRESULT specialLocationResult = SHGetFolderPath(nullptr, folderKind, nullptr, SHGFP_TYPE_CURRENT, path);

	wstring result;
	if (SUCCEEDED(specialLocationResult))
	{
		result = path;
	}
	return result;
}

// Returns a folder suitable for exe installer download destination.
// In case of error, shows a message box and returns an empty string.
std::wstring getDestDir(const GupNativeLang& nativeLang, const GupParameters& gupParams)
{
	// Note: Other fallback directories may be Downloads, %UserProfile%, etc.
	const wchar_t* envVar = _wgetenv(L"TEMP");
	if (envVar)
		return envVar;
	envVar = _wgetenv(L"TMP");
	if (envVar)
		return envVar;

	envVar = _wgetenv(L"AppData");
	std::wstring downloadFolder;
	if (envVar)
	{
		downloadFolder = envVar;
	}
	else
	{
		downloadFolder = getSpecialFolderLocation(CSIDL_APPDATA);
	}
	downloadFolder += L"\\Notepad++";
	if (::PathFileExists(downloadFolder.c_str()))
		return downloadFolder;

	downloadFolder = getSpecialFolderLocation(CSIDL_INTERNET_CACHE);
	if (!downloadFolder.empty())
		return downloadFolder;

	std::wstring message = nativeLang.getMessageString("MSGID_NODOWNLOADFOLDER");
	if (message.empty())
		message = MSGID_NODOWNLOADFOLDER;
	::MessageBox(NULL, message.c_str(), gupParams.getMessageBoxTitle().c_str(), MB_ICONERROR | MB_OK);
	return {};
}

std::wstring productVersionToFileVersion(const std::wstring& productVersion)
{
	auto pos = productVersion.find(L'.');
	if (pos == std::wstring::npos)
	{
		return productVersion + L".0.0.0";
	}

	// Assuming versions (minor, build, revision) will always be single digit for notepad++
	std::wstring minor = L"0";
	std::wstring build = L"0";
	std::wstring revision = L"0";

	std::wstring remaining = productVersion.substr(pos + 1);
	auto len = remaining.length();
	switch (len)
	{
	case 3:
		minor = remaining.at(0);
		build = remaining.at(1);
		revision = remaining.at(2);
		break;
	case 2:
		minor = remaining.at(0);
		build = remaining.at(1);
		break;
	case 1:
		minor = remaining.at(0);
		break;
	default:
		break;
	}

	std::wstring fileVersion = productVersion.substr(0, pos + 1);
	fileVersion += minor + L"." + build + L"." + revision;

	return fileVersion;
}

bool isAppProcess(const wchar_t* wszAppMutex)
{
	HANDLE hAppMutex = ::OpenMutexW(SYNCHRONIZE, false, wszAppMutex);
	if (hAppMutex)
	{
		::CloseHandle(hAppMutex);
		return true;
	}
	
	return false;
}

// Definition from Notepad++
#define NPPMSG  (WM_USER + 1000)
#define NPPM_DISABLEAUTOUPDATE (NPPMSG + 95) // 2119 in decimal
#define APP_MUTEX L"nppInstance"


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpszCmdLine, int)
{
	/*
	{
		wstring destPath = L"C:\\tmp\\res\\TagsView";
		wstring dlDest = L"C:\\tmp\\pb\\TagsView_Npp_03beta.zip";
		bool isSuccessful = decompress(dlDest, destPath);
		if (isSuccessful)
		{
			return 0;
		}
	}
	*/
	// Debug use - stop here so we can attach this process for debugging
	//::MessageBox(NULL, L"And do something dirty to me ;)", L"Attach me!", MB_OK);

	bool isSilentMode = false;
	FILE *pFile = NULL;
	
	wstring version;
	wstring customParam;

	wstring customInfoUrl;  // if not empty, it will override infoUrl value in gup.xml
	wstring forceDomain;    // if not empty, force GUP to ensure the download URL info belong to this domain.

	ParamVector params;
	parseCommandLine(lpszCmdLine, params);

	bool launchSettingsDlg = isInList(FLAG_OPTIONS, params);
	bool isVerbose = isInList(FLAG_VERBOSE, params);
	bool isHelp = isInList(FLAG_HELP, params);
	bool isCleanUp = isInList(FLAG_CLEANUP, params);
	bool isUnzip = isInList(FLAG_UUZIP, params);
	
	getParamVal('v', params, version);
	getParamVal('p', params, customParam);

	getParamValFromString(FLAG_INFOURL, params, customInfoUrl);
	getParamValFromString(FLAG_FORCEDOMAIN, params, forceDomain);

	SecurityGuard securityGuard;

	bool doCheckSignature = false;
	std::wstring checkSignatureStr;
	if (getParamValFromString(FLAG_CHKCERT_SIG, params, checkSignatureStr))
	{
		doCheckSignature = checkSignatureStr == L"yes";
	}

	if (isInList(FLAG_CHKCERT_TRUSTCHAIN, params))
	{
		securityGuard.enableChkTrustChain();
	}

	if (isInList(FLAG_CHKCERT_REVOC, params))
	{
		securityGuard.enableChkRevoc();
	}

	wstring signer_display_name;
	if (getParamValFromString(FLAG_CHKCERT_NAME, params, signer_display_name))
	{
		if (signer_display_name.length() >= 2 && (signer_display_name.front() == '"' && signer_display_name.back() == '"'))
		{
			signer_display_name = signer_display_name.substr(1, signer_display_name.length() - 2);
		}

		signer_display_name = stringReplace(signer_display_name, L"{QUOTE}", L"\"");

		securityGuard.setDisplayName(signer_display_name);
	}

	wstring signer_subject;
	if (getParamValFromString(FLAG_CHKCERT_SUBJECT, params, signer_subject))
	{
		if (signer_subject.length() >= 2 && (signer_subject.front() == '"' && signer_subject.back() == '"'))
		{
			signer_subject = signer_subject.substr(1, signer_subject.length() - 2);
		}

		signer_subject = stringReplace(signer_subject, L"{QUOTE}", L"\"");

		securityGuard.setSubjectName(signer_subject);
	}

	wstring signer_key_id;
	if (getParamValFromString(FLAG_CHKCERT_KEYID, params, signer_key_id))
	{
		securityGuard.setKeyId(signer_key_id);
	}

	wstring authority_key_id;
	if (getParamValFromString(FLAG_CHKCERT_AUTHORITYKEYID, params, authority_key_id))
	{
		securityGuard.setAuthorityKeyId(authority_key_id);
	}

	wstring errLogPath;
	if (getParamValFromString(FLAG_ERRLOGPATH, params, errLogPath))
	{
		if (errLogPath.length() >= 2 && (errLogPath.front() == '"' && errLogPath.back() == '"'))
		{
			errLogPath = errLogPath.substr(1, errLogPath.length() - 2);
		}
		securityGuard.setErrLogPath(errLogPath);
	}

	bool updateInfoXmlMustBeSigned = false;
	if (isInList(FLAG_CHKCERT_XML, params))
	{
		updateInfoXmlMustBeSigned = true;
	}

	wstring signer_key_id_xml;
	if (getParamValFromString(FLAG_CHKCERT_KEYID_XML, params, signer_key_id_xml))
	{
		securityGuard.setKeyIdXml(signer_key_id_xml);
	}

	// Object (gupParams) is moved here because we need app icon form configuration file
	GupParameters gupParams(L"gup.xml");
	appIconFile = gupParams.getSoftwareIcon();

	if (isHelp)
	{
		//::MessageBox(NULL, MSGID_HELP, L"GUP Command Argument Help", MB_OK);
		DialogBox(hInstance, MAKEINTRESOURCE(IDD_COMMANDLINEARGSBOX), NULL, helpDialogProc);
		return 0;
	}

	// 1st log msg, put there also the newline separator in front
	WRITE_LOG(GUP_LOG_FILENAME, L"\nlpszCmdLine: ", lpszCmdLine);

	GupExtraOptions extraOptions(L"gupOptions.xml");
	GupNativeLang nativeLang("nativeLang.xml");

	if (isAppProcess(APP_MUTEX))
	{
		std::wstringstream wss;
		wss << L"Maintained app with mutex \"" << APP_MUTEX << L"\" is still running.";
		WRITE_LOG(GUP_LOG_FILENAME, L"APP_MUTEX exists: ", wss.str().c_str());

		// wait for the Notepad++ app exit but max dwWaitMax ms
		// 
		// Note - Notepad++ mutex handling way:
		// - it does not store its created mutex HANDLE and does not set the CreateMutex 2nd bInitialOwner param (FALSE used)
		// - instead, it lets the system close that mutex HANDLE automatically when its last process instance terminates
		//   (mutex object is destroyed when its last opened HANDLE has been closed...)
		// - so waiting for the signaled state (not owned by any thread) of the Notepad++ mutex as a sync-method is not possible
		// 
		// - the used waiting method below is chosen on purpose (not using FindWindow/GetWindowThreadProcessId/OpenProcess/WaitForSingleObject way)
		//   as the FindWindow can be unreliable in this case (the main app wnd may not exist at this time, but the app process
		//   may still exist and e.g. locks its loaded plugins...)
		const DWORD dwWaitMax = 3000; // do not use longer times here as the GUP can be also launched with the Notepad++ behind on purpose
		const DWORD dwWaitStep = 250;
		DWORD dwWaited = 0;
		do
		{
			::Sleep(dwWaitStep);
			dwWaited += dwWaitStep;
		} while (isAppProcess(APP_MUTEX) && (dwWaited < dwWaitMax));

		if (dwWaited < dwWaitMax)
		{
			// ok
			WRITE_LOG(GUP_LOG_FILENAME, L"Waiting for the app-exit succeeded: ", L"Ok, maintained app is no longer running.");
		}
		else
		{
			// bad but there are also ops for which it is absolutely ok, so try to proceed
			WRITE_LOG(GUP_LOG_FILENAME, L"Waiting for the app-exit failed: ",
				L"The maintained app seems to be still running but GUP will try to proceed anyway...");
		}
	}

	//
	// Plugins Updater
	//
	size_t nbParam = params.size();

	// uninstall plugin:
	// gup.exe -clean "appPath2Launch" "dest_folder" "fold1" "a fold2" "fold3"
	// gup.exe -clean "c:\npp\notepad++.exe" "c:\temp\" "toto" "ti ti" "tata"
	if (isCleanUp && !isUnzip) // remove only
	{
		if (nbParam < 3)
		{
			WRITE_LOG(GUP_LOG_FILENAME, L"-1 in plugin updater's part - if (isCleanUp && !isUnzip) // remove only: ", L"nbParam < 3");
			return -1;
		}
		wstring prog2Launch = params[0];
		wstring destPathRoot = params[1];

#ifdef _DEBUG
		// Don't check any thing in debug mode
#else
		// Check signature of the launched program, with the same certif as gup.exe
		SecurityGuard securityGuard4PluginsInstall;

		if (!securityGuard4PluginsInstall.initFromSelfCertif())
		{
			securityGuard.writeSecurityError(L"Above certificate init error from \"gup -clean\" (remove plugins)", L"");
			return -1;
		}

		bool isSecured = securityGuard4PluginsInstall.verifySignedBinary(prog2Launch.c_str());
		if (!isSecured)
		{
			securityGuard.writeSecurityError(L"Above certificate verification error from \"gup -clean\" (remove plugins)", L"");
			return -1;
		}
#endif

		// clean
		for (size_t i = 2; i < nbParam; ++i)
		{
			wstring destPath = destPathRoot;
			::PathAppend(destPath, params[i]);
			deleteFileOrFolder(destPath);
		}

		safeLaunchAsUser(prog2Launch);

		return 0;
	}
	
	// update:
	// gup.exe -unzip -clean "appPath2Launch" "dest_folder" "pluginFolderName1 http://pluginFolderName1/pluginFolderName1.zip sha256Hash1" "pluginFolderName2 http://pluginFolderName2/pluginFolderName2.zip sha256Hash2" "plugin Folder Name3 http://plugin_Folder_Name3/plugin_Folder_Name3.zip sha256Hash3"
	// gup.exe -unzip -clean "c:\npp\notepad++.exe" "c:\donho\notepad++\plugins" "toto http://toto/toto.zip 7c31a97b..." "ti et ti http://ti_ti/ti_ti.zip 087a0591..." "tata http://tata/tata.zip 2e9766c..."

	// Install:
	// gup.exe -unzip "appPath2Launch" "dest_folder" "pluginFolderName1 http://pluginFolderName1/pluginFolderName1.zip sha256Hash1" "pluginFolderName2 http://pluginFolderName2/pluginFolderName2.zip sha256Hash2" "plugin Folder Name3 http://plugin_Folder_Name3/plugin_Folder_Name3.zip sha256Hash3"
	// gup.exe -unzip "c:\npp\notepad++.exe" "c:\donho\notepad++\plugins" "toto http://toto/toto.zip 7c31a97b..." "ti et ti http://ti_ti/ti_ti.zip 087a0591..." "tata http://tata/tata.zip 2e9766c..."
	if (isUnzip) // update or install
	{
		if (nbParam < 3)
		{
			WRITE_LOG(GUP_LOG_FILENAME, L"-1 in plugin updater's part - if (isCleanUp && isUnzip) // update: ", L"nbParam < 3");
			return -1;
		}

		wstring prog2Launch = params[0];
		wstring destPathRoot = params[1];

#ifdef _DEBUG
		// Don't check any thing in debug mode
#else
		// Check signature of the launched program, with the same certif as gup.exe
		SecurityGuard securityGuard4PluginsInstall;

		if (!securityGuard4PluginsInstall.initFromSelfCertif())
		{
			securityGuard.writeSecurityError(L"Above certificate init error from \"gup -unzip\" (install or update plugins)", L"");
			return -1;
		}

		bool isSecured = securityGuard4PluginsInstall.verifySignedBinary(prog2Launch.c_str());
		if (!isSecured)
		{
			securityGuard.writeSecurityError(L"Above certificate verification error from \"gup -unzip\" (install or update plugins)", L"");
			return -1;
		}
#endif
		for (size_t i = 2; i < nbParam; ++i)
		{
			wstring destPath = destPathRoot;

			// break down param in dest folder name and download url
			auto pos = params[i].find_last_of(L" ");
			if (pos != wstring::npos && pos > 0)
			{
				wstring folder;
				wstring dlUrl;

				wstring tempStr = params[i].substr(0, pos);
				wstring sha256ToCheck = params[i].substr(pos + 1, params[i].length() - 1);
				if (sha256ToCheck.length() != 64)
					continue;

				auto pos2 = tempStr.find_last_of(L" ");
				if (pos2 != string::npos && pos2 > 0)
				{
					// 3 parts - OK
					dlUrl = tempStr.substr(pos2 + 1, tempStr.length() - 1);
					folder = tempStr.substr(0, pos2);
				}
				else
				{
					// 2 parts - error. Just pass to the next
					continue;
				}

				::PathAppend(destPath, folder);

				// Make a backup path
				wstring backup4RestoreInCaseOfFailedPath;
				if (isCleanUp) // Update
				{
					//deleteFileOrFolder(destPath);

					// check if renamed folder exist, if it does, delete it
					backup4RestoreInCaseOfFailedPath = destPath + L".backup4RestoreInCaseOfFailed";
					if (::PathFileExists(backup4RestoreInCaseOfFailedPath.c_str()))
						deleteFileOrFolder(backup4RestoreInCaseOfFailedPath);

					// rename the folder with suffix ".backup4RestoreInCaseOfFailed"
					::MoveFile(destPath.c_str(), backup4RestoreInCaseOfFailedPath.c_str());
				}

				// install
				std::wstring dlDest = getDestDir(nativeLang, gupParams);
				if (dlDest.empty())
					return -1;
				dlDest += L"\\";
				dlDest += ::PathFindFileName(dlUrl.c_str());

				wchar_t *ext = ::PathFindExtension(dlDest.c_str());
				if (lstrcmp(ext, L".zip") != 0)
					dlDest += L".zip";

				dlFileName = ::PathFindFileName(dlUrl.c_str());

				wstring dlStopped = nativeLang.getMessageString("MSGID_DOWNLOADSTOPPED");
				if (dlStopped == L"")
					dlStopped = MSGID_DOWNLOADSTOPPED;

				bool isSuccessful = downloadBinary(dlUrl, dlDest, sha256ToCheck, pair<wstring, int>(extraOptions.getProxyServer(), extraOptions.getPort()), false, pair<wstring, wstring>(dlStopped, gupParams.getMessageBoxTitle()));
				if (isSuccessful)
				{
					isSuccessful = decompress(dlDest, destPath);
					if (!isSuccessful)
					{
						wstring unzipFailed = nativeLang.getMessageString("MSGID_UNZIPFAILED");
						if (unzipFailed == L"")
							unzipFailed = MSGID_UNZIPFAILED;

						::MessageBox(NULL, unzipFailed.c_str(), gupParams.getMessageBoxTitle().c_str(), MB_OK);

						// Delete incomplete unzipped folder
						deleteFileOrFolder(destPath);

						if (!backup4RestoreInCaseOfFailedPath.empty())
						{
							// rename back the folder
							::MoveFile(backup4RestoreInCaseOfFailedPath.c_str(), destPath.c_str());
						}
					}
					else
					{
						if (!backup4RestoreInCaseOfFailedPath.empty())
						{
							// delete the folder with suffix ".backup4RestoreInCaseOfFailed"
							deleteFileOrFolder(backup4RestoreInCaseOfFailedPath);
						}
					}

					// Remove downloaded zip from TEMP folder
					::DeleteFile(dlDest.c_str());
				}
				else
				{
					if (!backup4RestoreInCaseOfFailedPath.empty())
					{
						// delete the folder with suffix ".backup4RestoreInCaseOfFailed"
						::MoveFile(backup4RestoreInCaseOfFailedPath.c_str(), destPath.c_str());
					}
				}
			}
		}

		safeLaunchAsUser(prog2Launch);

		return 0;
	}

	//
	// Notepad++ Updater
	//
	g_hInst = hInstance;
	g_hAppWnd = ::FindWindowEx(NULL, NULL, gupParams.getClassName().c_str(), NULL); // for a possible interaction with the maintained app running behind
	try {
		if (launchSettingsDlg)
		{
			if (extraOptions.hasProxySettings())
			{
				proxySrv = extraOptions.getProxyServer();
				proxyPort = extraOptions.getPort();
			}
			if (::DialogBox(g_hInst, MAKEINTRESOURCE(IDD_PROXY_DLG), NULL, reinterpret_cast<DLGPROC>(proxyDlgProc)))
				extraOptions.writeProxyInfo(L"gupOptions.xml", proxySrv.c_str(), proxyPort);

			return 0;
		}

		msgBoxTitle = gupParams.getMessageBoxTitle();
		abortOrNot = nativeLang.getMessageString("MSGID_ABORTORNOT");

		//
		// Get update info
		//
		std::string updateInfo;

		// Get your software's current version.
		// If you pass the version number as the argument
		// then the version set in the gup.xml will be overrided
		if (!version.empty())
			gupParams.setCurrentVersion(version.c_str());

		// override silent mode if "-isVerbose" is passed as argument
		if (isVerbose)
			gupParams.setSilentMode(false);

		isSilentMode = gupParams.isSilentMode();

		bool getUpdateInfoSuccessful = getUpdateInfo(updateInfo, gupParams, extraOptions, customParam, version, customInfoUrl);

		if (!getUpdateInfoSuccessful)
		{
			WRITE_LOG(GUP_LOG_FILENAME, L"return -1 in Npp Updater part: ", L"getUpdateInfo func failed.");
			return -1;
		}

		if (updateInfoXmlMustBeSigned && !securityGuard.verifyXmlSignature(updateInfo))
		{
			return -1;
		}

		bool isModal = gupParams.isMessageBoxModal();
		GupDownloadInfo gupDlInfo(updateInfo.c_str());

		if (!gupDlInfo.doesNeed2BeUpdated())
		{
			if (!isSilentMode)
			{
				UpdateCheckParams localParams{ nativeLang, gupParams };
				::DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_UPDATE_DLG), isModal ? g_hAppWnd : NULL,
					reinterpret_cast<DLGPROC>(updateCheckDlgProc), reinterpret_cast<LPARAM>(&localParams));
			}
			return 0;
		}

		wstring downloadURL;
		if (!forceDomain.empty())
		{
			downloadURL = gupDlInfo.getDownloadLocation();

			if (downloadURL.size() <= forceDomain.size()                           // download URL must be longer than forceDomain
				|| downloadURL.compare(0, forceDomain.size(), forceDomain) != 0)   // Check if forceDomain is a prefix of download URL
			{
				securityGuard.writeSecurityError(L"Download URL does not match the expected domain:", downloadURL);
				return -1;
			}
		}

		//
		// Process Update Info
		//

		// Ask if user wants to do update

		UpdateAvailableDlgStrings uaDlgStrs;
		uaDlgStrs._title = nativeLang.getMessageString("MSGID_UPDATETITLE");


		wstring updateAvailable = nativeLang.getMessageString("MSGID_UPDATEAVAILABLE");
		if (updateAvailable.empty())
			updateAvailable = MSGID_UPDATEAVAILABLE;

		wstring versionCurrent = nativeLang.getMessageString("MSGID_VERSIONCURRENT");
		if (versionCurrent.empty())
			versionCurrent = MSGID_VERSIONCURRENT;
		versionCurrent += L" " + productVersionToFileVersion(version);

		wstring versionNew = nativeLang.getMessageString("MSGID_VERSIONNEW");
		if (versionNew.empty())
			versionNew = MSGID_VERSIONNEW;
		versionNew += L" " + gupDlInfo.getVersion();

		updateAvailable += L"\n\n" + versionCurrent;
		updateAvailable += L"\n" + versionNew;
		
		uaDlgStrs._message = updateAvailable;

		uaDlgStrs._customButton = nativeLang.getMessageString("MSGID_UPDATENEVER");
		uaDlgStrs._yesButton = nativeLang.getMessageString("MSGID_UPDATEYES");
		uaDlgStrs._yesSilentButton = nativeLang.getMessageString("MSGID_UPDATEYESSILENT");
		uaDlgStrs._noButton = nativeLang.getMessageString("MSGID_UPDATENo");

		int dlAnswer = 0;
		dlAnswer = static_cast<int32_t>(::DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_YESNONEVERDLG), g_hAppWnd, reinterpret_cast<DLGPROC>(yesNoNeverDlgProc), (LPARAM)&uaDlgStrs));

		if (dlAnswer == IDNO)
		{
			return 0;
		}
		
		if (dlAnswer == IDCANCEL)
		{
			if (g_hAppWnd)
			{
				::SendMessage(g_hAppWnd, NPPM_DISABLEAUTOUPDATE, 0, 0);
			}
			return 0;
		}
		else if (dlAnswer == IDOK)
		{
			nsisSilentInstallParam = FLAG_NSIS_SILENT_INSTALL_PARAM;
		}
		// else IDYES: do nothing

		//
		// Download executable bin
		//
		std::wstring dlDest = getDestDir(nativeLang, gupParams);
		if (dlDest.empty())
			return -1;
		dlDest += L"\\";
		dlDest += ::PathFindFileName(gupDlInfo.getDownloadLocation().c_str());

        wchar_t *ext = ::PathFindExtension(gupDlInfo.getDownloadLocation().c_str());
        if (lstrcmpW(ext, L".exe") != 0)
            dlDest += L".exe";

		dlFileName = ::PathFindFileName(gupDlInfo.getDownloadLocation().c_str());


		wstring dlStopped = nativeLang.getMessageString("MSGID_DOWNLOADSTOPPED");
		if (dlStopped == L"")
			dlStopped = MSGID_DOWNLOADSTOPPED;

		bool dlSuccessful = downloadBinary(gupDlInfo.getDownloadLocation(), dlDest, L"", pair<wstring, int>(extraOptions.getProxyServer(), extraOptions.getPort()), isSilentMode, pair<wstring, wstring>(dlStopped, gupParams.getMessageBoxTitle()));

		if (!dlSuccessful)
		{
			WRITE_LOG(GUP_LOG_FILENAME, L"return -1 in Npp Updater part: ", L"downloadBinary func failed.");
			return -1;
		}

		//
		// Check the code signing signature if demanded
		//
		if (doCheckSignature)
		{
			bool isSecured = securityGuard.verifySignedBinary(dlDest);

			if (!isSecured)
			{
				wstring dlFileSha256;

				std::string content = getFileContentA(ws2s(dlDest).c_str());
				if (content.empty())
				{
					dlFileSha256 = L"No SHA-256: the file is empty.";
				}
				else
				{
					char sha2hashStr[65] {};
					uint8_t sha2hash[32];
					calc_sha_256(sha2hash, reinterpret_cast<const uint8_t*>(content.c_str()), content.length());

					for (size_t i = 0; i < 32; i++)
					{
						sprintf(sha2hashStr + i * 2, "%02x", sha2hash[i]);
					}

					dlFileSha256 = L"Downloaded file SHA-256: ";
					dlFileSha256 += s2ws(sha2hashStr);
				}

				wstring dlUrl = L"DownloadURL: ";
				dlUrl += downloadURL;

				securityGuard.writeSecurityError(dlUrl, dlFileSha256);
				return -1;
			}
		}
		//
		// Run executable bin
		//
		wstring msg = gupParams.getClassName();
		wstring closeApp = nativeLang.getMessageString("MSGID_CLOSEAPP");
		if (closeApp == L"")
			closeApp = MSGID_CLOSEAPP;
		msg += closeApp;

		return runInstaller(dlDest, gupParams.getClassName(), msg, gupParams.getMessageBoxTitle().c_str());

	} catch (exception ex) {
		if (!isSilentMode)
			::MessageBoxA(NULL, ex.what(), "Xml Exception", MB_OK);

		if (pFile != NULL)
			fclose(pFile);

		WRITE_LOG(GUP_LOG_FILENAME, L"return -1 in Npp Updater part, exception: ", s2ws(ex.what()).c_str());
		return -1;
	}
	catch (...)
	{
		if (!isSilentMode)
			::MessageBoxA(NULL, "Unknown", "Unknown Exception", MB_OK);

		if (pFile != NULL)
			fclose(pFile);

		WRITE_LOG(GUP_LOG_FILENAME, L"return -1 in Npp Updater part, exception: ", L"Unknown Exception");
		return -1;
	}
}

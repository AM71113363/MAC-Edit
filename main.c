//lib -mwindows -lsetupapi -lcfgmgr32   
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <setupapi.h>
#include <ddk/cfgmgr32.h>
#include <ddk/ntddndis.h>
#include <ddk/ntddtdi.h>

#define WS_DROPALL           WS_CHILD|WS_VISIBLE |CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP

UCHAR NetWorkClassGuidID[] = "{4D36E972-E325-11CE-BFC1-08002BE10318}";


char szClassName[ ] = "WindowsApp";
HINSTANCE ins;
HWND hWnd;
HWND InterFaceID;

HWND hMacWindow[6];
HWND hIndexWindow;

HDEVINFO MainhDevInfo;

int OpenSystemDriver();
void CloseSystemDriver();
void CenterOnScreen(HWND hnd);


typedef struct DRIVERDATA_
{
     BYTE MAC[6]; 
     int index;
     unsigned char RegID[MAX_PATH];
}DRIVERDATA;

DRIVERDATA lista[10];
int listaLen = 0;

void AddToList(unsigned char *name, BYTE *mac, int index,unsigned char *RegId)
{   
    if(listaLen < 9)
    {
       memset(&lista[listaLen],0,sizeof(DRIVERDATA));
       lista[listaLen].MAC[0] = mac[0];
       lista[listaLen].MAC[1] = mac[1];
       lista[listaLen].MAC[2] = mac[2];
       lista[listaLen].MAC[3] = mac[3];
       lista[listaLen].MAC[4] = mac[4];
       lista[listaLen].MAC[5] = mac[5];
       
       lista[listaLen].index = index;
       sprintf(lista[listaLen].RegID,"%s\0",RegId);
       SendMessage(InterFaceID,CB_ADDSTRING,0,(LPARAM)(LPCSTR)name);
       listaLen++; 
     }
}


unsigned char *GetDriverInterfaces(unsigned char *driverNameGuid)
{
   static unsigned char name[MAX_PATH];
   HKEY  hKey;
   DWORD cbData, dwType;
   
   memset(name,0,MAX_PATH);
   sprintf(name,"SYSTEM\\CurrentControlSet\\Control\\Class\\%s",driverNameGuid);

   if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,name,0,KEY_READ,&hKey) == ERROR_SUCCESS)
   {
       memset(name,0,MAX_PATH);
       cbData = MAX_PATH-1;
       dwType = REG_SZ;
       if(RegQueryValueEx(hKey, "NetCfgInstanceId",0,&dwType,name,&cbData) == ERROR_SUCCESS)
       { 
             RegCloseKey(hKey);
             return name;
       }
       RegCloseKey(hKey);
    }
 return NULL;
}

int IsWiFiDevice(HDEVINFO hD, SP_DEVINFO_DATA spD)
{
    HKEY  hKey;
    int ret = -1;
    unsigned char  szSubKey[MAX_PATH];
    unsigned char  szPath[MAX_PATH];
    DWORD cbData = MAX_PATH-1;
    DWORD dwType = REG_SZ;
    
    memset(szPath,0,MAX_PATH);
    if(SetupDiGetDeviceRegistryProperty(hD,&spD, SPDRP_SERVICE, 0, (PBYTE)szPath,MAX_PATH,0))
    {
        if(szPath[0] == 0){ return -1; }
        
        memset(szSubKey,0,MAX_PATH);
        sprintf(szSubKey,"SYSTEM\\CurrentControlSet\\Services\\%s", szPath);
    
       if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, szSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
       {
           memset(szPath,0,MAX_PATH);
           if(RegQueryValueEx(hKey, "Group", 0,&dwType, szPath, &cbData) == ERROR_SUCCESS)
           {
              if(strcmp("NDIS",szPath) == 0){ ret = 1; }
           }
           RegCloseKey(hKey);
       }
    }
    return ret;
}



int GetDriverMAC(unsigned char *driverNameGuid, BYTE *pMac)
{
    static unsigned char devName[MAX_PATH];    
    HANDLE fd;
    DWORD len, r, bytesReturned = 0;
    BYTE address[6];
    unsigned char *p;
    DWORD oid = OID_802_3_CURRENT_ADDRESS;
    
    p = GetDriverInterfaces(driverNameGuid);
    if(p == NULL){ return -1; }
    memset(devName,0,MAX_PATH);
    len = sprintf(devName,"\\\\.\\%s",p);

    fd = CreateFile(devName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
    if(fd != INVALID_HANDLE_VALUE)
    {
         //IOCTL_NDIS_QUERY_GLOBAL_STATS() = 0x170002
         if(DeviceIoControl(fd,0x170002,&oid, sizeof(oid),address,6,&bytesReturned,0))
         {
             pMac[0] = address[0];
             pMac[1] = address[1];
             pMac[2] = address[2];
             pMac[3] = address[3];
             pMac[4] = address[4];
             pMac[5] = address[5];
             
              CloseHandle(fd);
              return 1;
         }
         CloseHandle(fd);
    }
  return -1;  
}

UCHAR *GetDeviceStringProperty(HDEVINFO Devs, PSP_DEVINFO_DATA DevInfo, DWORD Prop)
{
    UCHAR *buffer;
    DWORD size = 0;
    DWORD dataType;
    BOOL ret;

    SetupDiGetDeviceRegistryProperty(Devs,DevInfo,Prop,&dataType,(LPBYTE)buffer,size,&size);
    buffer=(UCHAR*)malloc(size+1);
    if(!buffer)
    {
        return NULL;
    }
    ret = SetupDiGetDeviceRegistryProperty(Devs,DevInfo,Prop,&dataType,(LPBYTE)buffer,size,&size);
    if( (ret == FALSE) || (dataType != REG_SZ) )
    {  
        return NULL;
    }
    buffer[size] = 0;
    return buffer;
}

UCHAR *DumpDevice(HDEVINFO Devs,PSP_DEVINFO_DATA DevInfo)
{
    UCHAR *desc = NULL;
    desc = GetDeviceStringProperty(Devs,DevInfo,SPDRP_FRIENDLYNAME);
    if(!desc){ desc = GetDeviceStringProperty(Devs,DevInfo,SPDRP_DEVICEDESC); }
    return desc;
}

int ReadAllInterfaces()
{
   // HDEVINFO hD;
    SP_DEVINFO_DATA spD;
    int i,ret;
    unsigned char *p;
    unsigned char temp[2048];
    unsigned  char tDint[MAX_PATH];
    BYTE tMac[6];
    
    i = 0;

    while (1)
    {
        memset(&spD,0,sizeof(SP_DEVINFO_DATA));
        spD.cbSize = sizeof(SP_DEVINFO_DATA);
        
        if(SetupDiEnumDeviceInfo(MainhDevInfo, i, &spD) == FALSE)
        {
            break;
        }
        memset(temp,0,2048);

        if(SetupDiGetDeviceRegistryProperty(MainhDevInfo,&spD,SPDRP_CLASSGUID,0,temp,2047,0))
        { 
        if(strcmp(NetWorkClassGuidID,temp) == 0)
        {
        if(IsWiFiDevice(MainhDevInfo,spD) == 1)
        {
        memset(temp,0,2048);
        if(SetupDiGetDeviceRegistryProperty(MainhDevInfo,&spD,SPDRP_DRIVER,0,temp,2047,0))
        {
            sprintf(tDint,"%s\0",temp);                                                                              
        ret= GetDriverMAC(temp,tMac);     
        if(ret > 0)
        {
           p = DumpDevice(MainhDevInfo,&spD);
           if(p)
           {
              AddToList(p, tMac, i,tDint);
              free(p);           
           } 
        }//GetDriverMAC                                                                                 
        }//SetupDiGetDeviceRegistryProperty                                 
        }//IsWiFiDevice
        }//strcmp
        }//SetupDiGetDeviceRegistryProperty        
        i++;
    }//while
    SendMessage(InterFaceID,CB_SETCURSEL, (WPARAM)0,0);
    return 0;    
}

void GetSelectedInfo()
{
     int selected = -1;
     int i;
     unsigned char mc[8];
     
     SetWindowText(hWnd,"MAC - CHANGE");
     selected = SendMessage(InterFaceID, CB_GETCURSEL, 0, 0);
     if(selected >= 0)
     {  
         
         sprintf(mc,"%02X\0",lista[selected].MAC[0]); SetWindowText(hMacWindow[0],mc);
         sprintf(mc,"%02X\0",lista[selected].MAC[1]); SetWindowText(hMacWindow[1],mc);
         sprintf(mc,"%02X\0",lista[selected].MAC[2]); SetWindowText(hMacWindow[2],mc);
         sprintf(mc,"%02X\0",lista[selected].MAC[3]); SetWindowText(hMacWindow[3],mc);
         sprintf(mc,"%02X\0",lista[selected].MAC[4]); SetWindowText(hMacWindow[4],mc);
         sprintf(mc,"%02X\0",lista[selected].MAC[5]); SetWindowText(hMacWindow[5],mc);     
     } 
     sprintf(mc,"%i\0",lista[selected].index); SetWindowText(hIndexWindow ,mc);    
     EnableWindow(GetDlgItem(hWnd,2000),1);
}

//------------------------------- disable enable ------------------------------------
void DisableNetWorkCard(HDEVINFO hD, SP_DEVINFO_DATA spD, BOOL bStatus)
{
    DWORD NewState;
    DWORD dwRetVal;

    SP_PROPCHANGE_PARAMS spPropChangeParams;
    SP_DEVINSTALL_PARAMS devParams;

    if(bStatus)
    {
       NewState = DICS_DISABLE;
    }
    else
    {
       NewState = DICS_ENABLE;
    }               
    
    
         spPropChangeParams.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);
         spPropChangeParams.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;
         spPropChangeParams.Scope=DICS_FLAG_GLOBAL;
         spPropChangeParams.StateChange=NewState;
         spPropChangeParams.HwProfile = 0;
         
         if(SetupDiSetClassInstallParams(hD,&spD,
                                          (SP_CLASSINSTALL_HEADER*)&spPropChangeParams,
                                          sizeof(spPropChangeParams)))
         {
               SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,hD,&spD);                          
         }
         spPropChangeParams.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);
         spPropChangeParams.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;
         spPropChangeParams.Scope=DICS_FLAG_CONFIGSPECIFIC;
         spPropChangeParams.StateChange=NewState;
         spPropChangeParams.HwProfile = 0;       
         
         if(!SetupDiSetClassInstallParams(hD,&spD,
                                          (SP_CLASSINSTALL_HEADER*)&spPropChangeParams,
                                          sizeof(spPropChangeParams)) ||
         !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,hD,&spD))                         
         {
             DumpDevice(hD,&spD);
         }
         else
         {                                                                                                
           devParams.cbSize = sizeof(devParams);
           SetupDiGetDeviceInstallParams(hD,&spD,&devParams) && (devParams.Flags & (DI_NEEDRESTART|DI_NEEDREBOOT));
          }
}

//---------------------------------------------------------------------------------------


int AddOne(int index,int step)
{
    if(step < 1) return -1;
    if(lista[index].MAC[step] > 0xFD){ return AddOne(index,step-1); }
    lista[index].MAC[step] = lista[index].MAC[step] + 1;
    return 1;
}

int GetNew(int index,int n)
{
    UCHAR MC[4];
    int ret;
    ret = GetWindowText(hMacWindow[n],MC,3);
    if(ret != 2)
       return 0;
    MC[3] = 0;
    sscanf(MC,"%X",&(lista[index].MAC[n]));
    return 1;
}

int ModifyRegistry(int index)
{
     int step = 5;
     HKEY hKey;
     UCHAR bu[16];
     UCHAR tp[MAX_PATH];
     
     int ret;
         
     sprintf(tp,"SYSTEM\\ControlSet001\\Control\\Class\\%s\0",lista[index].RegID);
     if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, tp, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
     {
           SetWindowText(hWnd,"ERROR: open keyA");
           _sleep(1000);
           return 0;
     }
     if(GetNew(index, 0) != 1)
       return 0;     
     if(GetNew(index, 1) != 1)
       return 0;     
     if(GetNew(index, 2) != 1)
       return 0;     
     if(GetNew(index, 3) != 1)
       return 0;     
     if(GetNew(index, 4) != 1)
       return 0;     
     if(GetNew(index, 5) != 1)
       return 0;     
    
     AddOne(index,step); 
     sprintf(bu,"%02X%02X%02X%02X%02X%02X\0",lista[index].MAC[0],
											 lista[index].MAC[1],
											 lista[index].MAC[2],
											 lista[index].MAC[3],
											 lista[index].MAC[4],
											 lista[index].MAC[5]);
	 bu[12] = 0; 
     RegSetValueEx(hKey, "NetworkAddress", 0, REG_SZ, (LPBYTE)bu, 12);
     RegCloseKey(hKey);
   
     sprintf(tp,"SYSTEM\\CurrentControlSet\\Control\\Class\\%s\0",lista[index].RegID);
     if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, tp, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
     {
           SetWindowText(hWnd,"ERROR: open keyA");
           _sleep(1000);
           return 0;
     }
     RegSetValueEx(hKey, "NetworkAddress", 0, REG_SZ, (LPBYTE)bu, 12);
     RegCloseKey(hKey); 
     return 1; 
}


void ResetDriverSelected()
{
    SP_DEVINFO_DATA spD;
    int selected = -1;
    unsigned char mc[8];
        
    memset(&spD,0,sizeof(SP_DEVINFO_DATA));
    spD.cbSize = sizeof(SP_DEVINFO_DATA);

    selected = SendMessage(InterFaceID, CB_GETCURSEL, 0, 0);
    if(selected < 0)
    {
         SetWindowText(hWnd,"Error : NoSelection--BUG"); 
         return;
    }
    if(SetupDiEnumDeviceInfo(MainhDevInfo, lista[selected].index, &spD) == FALSE)
    {
         SetWindowText(hWnd,"Error : SetupDiEnumDeviceInfo(index)"); 
         return;
    }
    
    SetWindowText(hWnd,"Disable NetWork..."); 
    DisableNetWorkCard(MainhDevInfo, spD, TRUE);
    SetWindowText(hWnd,"Sleep..."); 
    _sleep(2000);
    SetWindowText(hWnd,"Modify..."); 
    if(ModifyRegistry(selected) != 1) SetWindowText(hWnd,"Error: Modify"); 
    _sleep(2000);
    SetWindowText(hWnd,"Enable NetWork..."); 
    DisableNetWorkCard(MainhDevInfo, spD, FALSE);
    SetWindowText(hWnd,"Exit...");  
    _sleep(2000);
    SNDMSG(hWnd,WM_DESTROY,0,0);
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)                  /* handle the messages */
    {
       case WM_CREATE:
       {
            HFONT hFont;
            hWnd = hwnd;
            hFont = CreateFont(14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Comic Sans MS");
            InterFaceID=CreateWindow("combobox", "",WS_DROPALL,2, 2, 247, 280, hwnd, 0,ins, NULL);  	
            CreateWindow("BUTTON","info",WS_CHILD|WS_VISIBLE,253,2,35,24,hwnd,(HMENU)1000,ins,NULL);	
            CreateWindow("BUTTON","CHANGE",WS_CHILD|WS_VISIBLE|WS_DISABLED,191,27,97,18,hwnd,(HMENU)2000,ins,NULL);	
            SendMessage(InterFaceID, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(1, 0));  
       
            hIndexWindow = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 4,28,32,15,hwnd,(HMENU)0,ins,NULL);
    
            hMacWindow[0] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 44,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[0],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
            hMacWindow[1] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 68,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[1],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
            hMacWindow[2] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 92,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[2],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
            hMacWindow[3] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 116,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[3],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
            hMacWindow[4] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 140,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[4],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
            hMacWindow[5] = CreateWindow("EDIT","",WS_VISIBLE | WS_CHILD, 164,28,20,15,hwnd,(HMENU)0,ins,NULL);
            SendMessage(hMacWindow[5],EM_SETLIMITTEXT,(WPARAM)2,(LPARAM)0); 
             
            
            if(OpenSystemDriver() < 1)
              exit(0); 
            CreateThread(0,0,(LPTHREAD_START_ROUTINE)ReadAllInterfaces,0,0,0); 
            CenterOnScreen(hwnd);
       }
       break;
             case WM_COMMAND:
      {
      switch(LOWORD(wParam))
      {            
             case 1000:
             {  
                      CreateThread(0,0,(LPTHREAD_START_ROUTINE)GetSelectedInfo,0,0,0);   
             }
             break;             
             case 2000:
             {  
                      EnableWindow(GetDlgItem(hwnd,2000),0);
                      CreateThread(0,0,(LPTHREAD_START_ROUTINE)ResetDriverSelected,0,0,0);   
             }
             break;  

      }//switch
      }//WM_COMMAND
      break; 
        case WM_DESTROY:
        {
            SetupDiDestroyDeviceInfoList(MainhDevInfo);
            PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
        }
            break;
        default:                      /* for messages that we don't deal with */
            return DefWindowProc (hwnd, message, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain (HINSTANCE hThisInstance,
                    HINSTANCE hPrevInstance,
                    LPSTR lpszArgument,
                    int nFunsterStil)

{
    HWND hwnd;
    MSG messages;
    WNDCLASSEX wincl;
    ins = hThisInstance;
    wincl.hInstance = hThisInstance;
    wincl.lpszClassName = szClassName;
    wincl.lpfnWndProc = WindowProcedure;
    wincl.style = CS_DBLCLKS;
    wincl.cbSize = sizeof (WNDCLASSEX);
    wincl.hIcon = LoadIcon(ins, "A");
    wincl.hIconSm = LoadIcon(ins, "A");
    wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
    wincl.lpszMenuName = NULL;
    wincl.cbClsExtra = 0;
    wincl.cbWndExtra = 0;
    wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

    if (!RegisterClassEx (&wincl))
        return 0;

    hwnd = CreateWindowEx (WS_EX_TOPMOST,                   /* Extended possibilites for variation */
           szClassName,         /* Classname */
           "MAC - CHANGE",       /* Title Text */
           WS_VISIBLE |WS_SYSMENU|WS_MINIMIZEBOX, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           297,                 /* The programs width */
           78,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           NULL,                /* No menu */
           hThisInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );

    while (GetMessage (&messages, NULL, 0, 0))
    {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }
   return messages.wParam;
}


int OpenSystemDriver()
{
    MainhDevInfo = SetupDiGetClassDevs(0, 0, NULL, DIGCF_PRESENT |DIGCF_ALLCLASSES);
    if(MainhDevInfo == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
return 1;
}

void CloseSystemDriver()
{
  SetupDiDestroyDeviceInfoList(MainhDevInfo);
}


void CenterOnScreen(HWND hnd)
{
  RECT rcClient, rcDesktop;
  int nX,nY;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &rcDesktop, 0);
  GetWindowRect(hnd, &rcClient);
  nX=((rcDesktop.right - rcDesktop.left) / 2) -((rcClient.right - rcClient.left) / 2);
  nY=((rcDesktop.bottom - rcDesktop.top) / 2) -((rcClient.bottom - rcClient.top) / 2);
  SetWindowPos(hnd, NULL, nX, nY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
return;
}


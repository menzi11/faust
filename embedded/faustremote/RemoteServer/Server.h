//
//  Server.h
//
//  Created by Sarah Denoux on 13/05/13.
//  Copyright (c) 2013 GRAME. All rights reserved.
//

#ifndef _SERVER_h
#define _SERVER_h

#include <sstream>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <stdlib.h>
#include <list>
#include <map>
#include <vector>
#include <microhttpd.h>

#ifdef __APPLE__
#include <dns_sd.h>
#endif

#include "faust/audio/netjack-dsp.h"
#include "llvm-dsp.h"
#include "utilities.h"
#include "TMutex.h"

#define POSTBUFFERSIZE 512
#define GET 0
#define POST 1

#ifdef WIN32
    #define	EXPORT __declspec(dllexport)
#else
	#define	EXPORT __attribute__ ((visibility("default")))
#endif

class DSPServer;

using namespace std;

class netjackaudio_server : public netjackaudio_midicontrol {  

    private:

        int fNumberRestartAttempts;

    public:
    
        netjackaudio_server(int celt, const std::string& master_ip, int master_port, int mtu, int latency)
            :netjackaudio_midicontrol(celt, master_ip, master_port, mtu, latency)
        {
            fNumberRestartAttempts = 0;
        }
        
        void error_cb(int error_code)
        {
            switch (error_code) {
            
                case SOCKET_ERROR:
                    printf("NetJack : SOCKET_ERROR\n");
                    break;
                    
                case SYNC_PACKET_ERROR:
                    printf("NetJack : SYNC_PACKET_ERROR\n");
                    break;

                 case DATA_PACKET_ERROR:
                    printf("NetJack : DATA_PACKET_ERROR\n");
                    break;
            }
        }
    
        /*
        virtual int restart_cb()
        {
            printf("NetJack : restart_cb\n");
            return 0;
        }
        */

};

// Structure handled by libmicrohttp related to a connection

struct connection_info_struct {
    
    int                 fConnectiontype;    // GET or POST
    
    MHD_PostProcessor*  fPostprocessor;     // the POST processor used internally by microhttpd
    int                 fAnswercode;        // used internally by microhttpd to see where things went wrong or right
    
    std::string         fAnswerstring;      // the answer sent to the user after upload
    
    //-----DATAS RECEIVED TO CREATE NEW DSP FACTORY---------
    string              fNameApp;
    string              fFaustCode;
    string              fFactoryKey;
    vector<string>      fCompilationOptions;
    string              fOptLevel;
    
    llvm_dsp_factory*   fLLVMFactory;
    //---------------------------------------------
    
    //------DATAS RECEIVED TO CREATE NEW DSP INSTANCE-------
    string              fIP;
    string              fPort;
    string              fCompression;
    string              fMTU;
    string              fLatency;
    string              fSHAKey;
    string              fInstanceKey;
    //--------------------------------------------- 
    
    connection_info_struct();
    ~connection_info_struct();
    
};
    
#include <netdb.h>
#include <arpa/inet.h>

// Structure wrapping llvm_dsp with all its needed elements (audio/interface/...)

struct netjack_dsp {
    
    string          fInstanceKey;
    string          fName;
    
    // NETJACK PARAMETERS
    string          fIP;
    string          fPort;
    string          fCompression;
    string          fMTU;
    string          fLatency;
    
    netjackaudio_server*   fAudio; //NETJACK SLAVE 
    llvm_dsp*              fDSP;   //Real DSP Instance 
    
    //To be sure not access the same resources at the same time, the mutex of the server has to be accessible here
    //So that the server himself is kept
    DSPServer*      fDSPServer;
    
    netjack_dsp(llvm_dsp_factory* smartFactory, 
            const string& compression, 
            const string& ip, const string& port, 
            const string& mtu, const string& latency, 
            DSPServer* server);
    virtual ~netjack_dsp();
    
    bool start();
    void stop();
    
    string  getKey() { return fInstanceKey; }
    void    setKey(const string& key) { fInstanceKey = key; }
    string  getName() { return fName; }
    void    setName(string name) { fName = name; }
    
    bool openAudioConnection();
};
    
// Same Prototype LLVM/REMOTE dsp are using for allocation/desallocation

class DSPServer {

    friend struct netjack_dsp;
        
    private:

        pthread_t fThread;
        TMutex    fLocker;
        int       fPort;
        
        // Factories that can be instanciated. 
        // The remote client asking for a new DSP Instance has to send an index corresponding to an existing factory
        // SHAKey, pair<NameApp, Factory>
        map<string, pair<string, llvm_dsp_factory*> > fAvailableFactories;
            
        // List of currently running DSP. Use to keep track of Audio that would have lost their connection
        list<netjack_dsp*> fRunningDsp;
        struct MHD_Daemon* fDaemon; //Running http daemon
        
        // Callback of another thread to wait netjack audio connection without blocking the server
        static void*    openAudioConnection(void*);
                
        // Creates the html to send back
        int             sendPage(MHD_Connection* connection, const string& page, int status_code, const string& type);
            
        void            stopNotActiveDSP();
            
        connection_info_struct* allocateConnectionStruct(MHD_Connection* connection, const char* method);
            
        // Reaction to any kind of connection to the Server
        static int      answerToConnection(void* cls, MHD_Connection* connection, 
                                            const char* url, const char* method, 
                                            const char* version, const char* upload_data, 
                                            size_t* upload_data_size, void** con_cls);
            
            
        // Reaction to a GET request
        int             answerGet(MHD_Connection* connection, const char* url);
            
        // Reaction to a POST request
        int             answerPost(MHD_Connection* connection, const char* url, 
                                    const char* upload_data, size_t *upload_data_size, 
                                    void** con_cls);
            
        // Callback that processes the data send to the server
        static int iteratePost(void* coninfo_cls, MHD_ValueKind kind, const char* key, 
                                const char* filename, const char* content_type, 
                                const char* transfer_encoding, const char* data, 
                                uint64_t off, size_t size);
            
        static void requestCompleted(void* cls, MHD_Connection* connection, void** con_cls, MHD_RequestTerminationCode toe);
            
        // Reaction to a /GetJson request --> Creates llvm_dsp_factory & json interface
        bool        createFactory(connection_info_struct* con_info);
        // Reaction to a /GetJsonFromKey --> GetJson form available factory
        bool        getJsonFromKey(connection_info_struct* con_info);
            
        // Reaction to a /CreateInstance request --> Creates llvm_dsp_instance & netjack slave
        bool        createInstance(connection_info_struct* con_info);
        
        bool        startAudio(const string& shakey);
        
        void        stopAudio(const string& shakey);
        
        int         createConnection(MHD_Connection* connection, const char* method, void** con_cls);
        
        // Register Service as Available
        static void* registration(void* arg);

    public:
            
        DSPServer(int argc, const char* argv[]);
        virtual ~DSPServer();
            
        // Start server on specified port 
        bool start(int port = 7777);
        void stop();
    
};

// Public C++ API

class EXPORT remote_dsp_server {
    
    public: 
        
        bool start(int port);
        void stop();
};

EXPORT remote_dsp_server* createRemoteDSPServer(int argc, const char* argv[]);

EXPORT void deleteRemoteDSPServer(remote_dsp_server* compiler);
    
#endif
    
    
    
    
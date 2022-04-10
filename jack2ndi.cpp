/*
 * NDI Client to JACK (Jack Audio Connection Kit) Output
 * 
 * This program can be used and distrubuted without resrictions
 */

#include <cassert>
#include <cstdio>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <math.h>
#include <signal.h>
#include <ctime>
#include <atomic>
#include <unistd.h>
#include <fstream> //for reading and writing preset file
#include <thread>

#include <getopt.h> 
#include <condition_variable>

#include <Processing.NDI.Lib.h>
#include <jack/jack.h>

bool auto_connect_jack_ports = true;

static char             *ndi_name;
static char             *client_name;

//Function Definitions
int process_callback(jack_nframes_t x, void *p);



struct send_audio {
 send_audio(const char *client_name="NDI_send",const char *ndi_server_name="NDI_send",bool auto_connect_ports=true); //constructor
 ~send_audio(void); //destructor 
 public:
  int process(jack_nframes_t nframes);
 private:	
	NDIlib_send_instance_t m_pNDI_send; //create the NDI sender
  NDIlib_audio_frame_v2_t m_NDI_audio_frame; //create the audio frame for sending
  jack_port_t *in_port1, *in_port2;
  jack_client_t *jack_client;
  jack_nframes_t jack_sample_rate;
	std::atomic<bool> m_exit;	// Are we ready to exit		
  static void jack_shutdown(void *arg); //This is called when JACK is shutdown
};

int send_audio::process(jack_nframes_t nframes){
  jack_default_audio_sample_t *in1, *in2;
  //Get JACK Audio Buffers
  in1 = (jack_default_audio_sample_t*)jack_port_get_buffer (in_port1, nframes);
  in2 = (jack_default_audio_sample_t*)jack_port_get_buffer (in_port2, nframes);
  
  m_NDI_audio_frame.no_samples = nframes;

  m_NDI_audio_frame.p_data = (float*)malloc(nframes * 2 * sizeof(float));
	m_NDI_audio_frame.channel_stride_in_bytes = nframes * sizeof(float);
  //NDIlib_audio_frame_v2_t audio_frame;
  //NDIlib_framesync_capture_audio(m_pNDI_framesync, &audio_frame, jack_sample_rate, 2, nframes);
  //printf("Audio data received (%d samples).\n", audio_frame.no_samples);
  //std::cout << "Number of audio frames (JACK): " << nframes << std::endl;
  //std::cout << "Audio Frame Data (NDI): " << audio_frame.p_data << std::endl;
  //std::cout << "Channel Stride in Bytes (NDI): " << audio_frame.channel_stride_in_bytes << std::endl;
  //std::cout << "Size of Audio Frame (NDI): " << sizeof(audio_frame.p_data) << std::endl;
  //std::cout << "Number of Audio Channels (NDI): " << sizeof(audio_frame.no_channels) << std::endl;
  if(m_NDI_audio_frame.p_data != 0){ //make sure that there is data in the buffer before trying to copy anything
   float* p_ch1 = (float*)((uint8_t*)m_NDI_audio_frame.p_data + 0*m_NDI_audio_frame.channel_stride_in_bytes); //Get Channel 1 pointer from NDI audio frame
   float* p_ch2 = (float*)((uint8_t*)m_NDI_audio_frame.p_data + 1*m_NDI_audio_frame.channel_stride_in_bytes); //Get Channel 2 pointer from NDI audio frame
   memcpy(p_ch1, in1, sizeof(jack_default_audio_sample_t) * nframes); //copy the audio frame from JACK buffer to the NDI frame
   memcpy(p_ch2, in2, sizeof(jack_default_audio_sample_t) * nframes); //copy the audio frame from JACK buffer to the NDI frame
  }
  // Send the NDI audio frame
  NDIlib_send_send_audio_v2(m_pNDI_send, &m_NDI_audio_frame);
  free(m_NDI_audio_frame.p_data); //free the audio frame
  return 0;      
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void send_audio::jack_shutdown(void *arg){
  exit(1);
}

//Constructor
send_audio::send_audio(const char *client_name, const char *ndi_server_name, bool auto_connect_ports): m_pNDI_send(NULL), m_exit(false), jack_client(NULL), in_port1(NULL), in_port2(NULL){
  printf("Starting Sender for %s\n", ndi_server_name);
  printf("Connecting to JACK as %s\n", client_name);
  const char **ports;
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  // Create an NDI source
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.p_ndi_name = ndi_server_name;
	//NDI_send_create_desc.clock_audio = true;
  
  //Create the NDI sender using the description
  m_pNDI_send = NDIlib_send_create(&NDI_send_create_desc);

  /* open a client connection to the JACK server */
  jack_client = jack_client_open (client_name, options, &status, server_name);
  if(jack_client == NULL){
   fprintf (stderr, "jack_client_open() failed, ""status = 0x%2.0x\n", status);
   if(status & JackServerFailed){
	  fprintf (stderr, "Unable to connect to JACK server\n");
   }
   exit (1);
  }
  if(status & JackServerStarted){
   fprintf (stderr, "JACK server started\n");
  }
  if(status & JackNameNotUnique){
   //client_name = jack_get_client_name(jack_client);
   //fprintf (stderr, "unique name `%s' assigned\n", client_name);
  }

  jack_sample_rate = jack_get_sample_rate(jack_client);
  
  jack_set_process_callback (jack_client, ::process_callback, this); //This callback is called on every every time JACK does work - every audio sample
  jack_on_shutdown (jack_client, send_audio::jack_shutdown, 0); //JACK shutdown callback - gets called on JACK shutdown
  
  /* create two input JACK ports */
  in_port1 = jack_port_register (jack_client, "input1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  in_port2 = jack_port_register (jack_client, "input2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  
  if((in_port1 == NULL) || (in_port2 == NULL)){ //can't create JACK input ports
   fprintf(stderr, "no more JACK ports available\n");
   exit (1);
  }

  /* Tell the JACK server that we are ready to roll.  Our
   * process() callback will start running now. */
  if(jack_activate (jack_client)){
   fprintf (stderr, "cannot activate client");
   exit (1);
  }

  /* Connect the ports.  You can't do this before the client is
   * activated, because we can't make connections to clients
   * that aren't running.  Note the confusing (but necessary)
   * orientation of the driver backend ports: playback ports are
   * "input" to the backend, and capture ports are "output" from
   * it.
   */
  if(auto_connect_ports == true){ //make sure that auto connect of JACK ports is enabled
   ports = jack_get_ports (jack_client, NULL, NULL, JackPortIsPhysical|JackPortIsOutput);
   if(ports == NULL){
    fprintf(stderr, "no physical capture ports\n");
    exit (1);
   }

   if(jack_connect (jack_client, ports[0], jack_port_name (in_port1))){
    fprintf(stderr, "cannot connect input ports\n");
   }

   if(jack_connect (jack_client, ports[1], jack_port_name (in_port2))){
    fprintf (stderr, "cannot connect input ports\n");
   }

   jack_free (ports);
  }

  m_NDI_audio_frame.sample_rate = jack_sample_rate;
	m_NDI_audio_frame.no_channels = 2;
}

// Destructor
send_audio::~send_audio(void){	// Wait for the thread to exit
	m_exit = true;
	jack_client_close(jack_client);
	// Destroy the receiver
  
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 */
int process_callback(jack_nframes_t x, void *p){
 return static_cast<send_audio*>(p)->process(x); 
}

static const int no_senders = 30; //max number of senders
send_audio* p_senders[no_senders] = { 0 };
std::string ndi_running_name[no_senders] = { "" };

static void usage(FILE *fp, int argc, char **argv){
        fprintf(fp,
                 "Usage: JACK to NDI [options]\n\n"
                 "Version 1.1\n"
                 "Options:\n"
                 "-h | --help          Print this message\n"
                 "-n | --ndi-name      NDI output stream name\n"
                 "-j | --jack-name     JACK client name\n"
                 "-a | --auto-connect  Disable auto connect JACK ports (default to true)\n"
                 "",
                 argv[0]);
}

static const char short_options[] = "n:j:a";

static const struct option
long_options[] = {
        { "help",   no_argument,       NULL, 'h' },
        { "ndi-name", required_argument, NULL, 'n' },
        { "jack-name", required_argument, NULL, 'j' },
        { "auto-connect", no_argument,       NULL, 'a' },
        { 0, 0, 0, 0 }
};

int main (int argc, char **argv){
  ndi_name = (char*)"Stream"; //default NDI stream name
  client_name = (char*)"NDI_send"; //default JACK client name
  for (;;) {
   int idx;
   int c;
   c = getopt_long(argc, argv,short_options, long_options, &idx);
   if (-1 == c){
    break;
   }
   switch(c){
    case 'h':
     usage(stdout, argc, argv);
     exit(EXIT_SUCCESS);
    case 'n':
     ndi_name = optarg;  
    case 'j':
     client_name = optarg;   
    case 'a':
     auto_connect_jack_ports = false;
     break;           
    default:
     usage(stderr, argc, argv);
     exit(EXIT_FAILURE);
   }
  }
  
  if(!NDIlib_initialize()){	
	 printf("Cannot run NDI."); // Cannot run NDI. Most likely because the CPU is not sufficient.
	 return 0;
	}

	// Create a NDI finder	
	
   p_senders[0] = new send_audio(client_name,ndi_name);
                               
  /* keep running until the Ctrl+C */
  while(1){
   sleep(1);
  }
  
  exit (0);
}

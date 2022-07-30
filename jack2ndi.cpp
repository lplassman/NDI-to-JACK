/*
 * JACK to NDI Output
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
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <getopt.h>

#include <Processing.NDI.Lib.h>
#include <jack/jack.h>

bool auto_connect_jack_ports = false;

static char             *ndi_name;
static char             *client_name;

//Function Definitions
int process_callback(jack_nframes_t x, void *p);



struct send_audio {
 send_audio(const char *c_name="ndi",const char *n_name="NDI_send",bool a_ports=false); //constructor
 ~send_audio(void); //destructor 
 public:
  int process(jack_nframes_t nframes);
  void queue_wait(void);
  void process_audio_thread(void);
  void queue_push(jack_default_audio_sample_t** frame);
  jack_default_audio_sample_t** queue_pop_opt(void);
 private:	
	NDIlib_send_instance_t m_pNDI_send; //create the NDI sender
  NDIlib_audio_frame_v2_t m_NDI_audio_frame; //create the audio frame for sending
  jack_port_t **in_ports;
  jack_default_audio_sample_t **in;
  jack_client_t *jack_client;
  jack_nframes_t jack_sample_rate;
  int num_channels = 2;
  jack_nframes_t num_frames;
  std::thread audio_thread;
  std::size_t m_max_depth = 1;    // How many items we will queue before dropping them
  std::mutex m_lock;
  std::condition_variable m_condvar;
  std::queue<jack_default_audio_sample_t**> m_queue;
	std::atomic<bool> m_exit;	// Are we ready to exit		
  static void jack_shutdown(void *arg); //This is called when JACK is shutdown
};

void send_audio::queue_wait(void){
  std::unique_lock<std::mutex> lock_queue(m_lock); //lock the queue
  while (m_queue.empty()){ //wait until there is a new frame
    m_condvar.wait(lock_queue);
  }
}

void send_audio::queue_push(jack_default_audio_sample_t** frame){
  std::unique_lock<std::mutex> lock_queue(m_lock); //Lock the queue
  m_queue.push(std::move(frame)); // Queue the frame

  // Drop items that are too old if the queue is not keeping up
  while ((m_max_depth) && (m_queue.size() > m_max_depth)){
    m_queue.pop(); //drop an audio frame from the queue
    printf("!"); fflush(stdout);
  }
  lock_queue.unlock(); //unlock the queue
  m_condvar.notify_one(); //notify the listener that there is data
}

jack_default_audio_sample_t** send_audio::queue_pop_opt(void){
 // Lock the queue
 std::unique_lock<std::mutex> lock_queue(m_lock); 
 if(m_queue.empty()) {
  return {};
 }
 //Get the item from the queue
 jack_default_audio_sample_t** item = std::move(m_queue.front());
 m_queue.pop();
 lock_queue.unlock(); //unlock the queue
 return item;
}

int send_audio::process(jack_nframes_t nframes){
  //Get JACK Audio Buffers
  for (int channel = 0; channel < num_channels; channel++){
   in[channel] = (jack_default_audio_sample_t*)jack_port_get_buffer (in_ports[channel], nframes);
  }  
  send_audio::queue_push(std::move(in));
  num_frames = nframes;
  return 0;      
}

void send_audio::process_audio_thread(void){
  bool exit_thread = false;
  while (true){
    queue_wait(); //wait until there is some data to process
    while (true){
     auto frame = queue_pop_opt(); //get the data frame off of the queue
     if(!frame){ //no data - wait for more data at queue_wait()
      break; 
     } 
     m_NDI_audio_frame.no_samples = num_frames;
     m_NDI_audio_frame.p_data = (float*)malloc(num_frames * num_channels * sizeof(float));
	   m_NDI_audio_frame.channel_stride_in_bytes = num_frames * sizeof(float);

    if(m_NDI_audio_frame.p_data != 0){ //make sure that there is data in the buffer before trying to copy anything
     for (int channel = 0; channel < num_channels; channel++){
      float* p_ch = (float*)((uint8_t*)m_NDI_audio_frame.p_data + channel*m_NDI_audio_frame.channel_stride_in_bytes); //Initialize channels in NDI frame
      memcpy(p_ch, frame[channel], sizeof(jack_default_audio_sample_t) * num_frames); //copy the audio frame from JACK buffer to the NDI frame
     }
    }
    // Send the NDI audio frame
    NDIlib_send_send_audio_v2(m_pNDI_send, &m_NDI_audio_frame);
    free(m_NDI_audio_frame.p_data); //free the audio frame
   }
  }
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void send_audio::jack_shutdown(void *arg){
  exit(1);
}

//Constructor
send_audio::send_audio(const char *c_name, const char *n_name, bool a_ports): m_pNDI_send(NULL), m_exit(false), jack_client(NULL){
  printf("Starting Sender for %s\n", n_name);
  printf("Connecting to JACK as %s\n", c_name);
  const char **ports;
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  // Create an NDI source
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.p_ndi_name = n_name;
	//NDI_send_create_desc.clock_audio = true;
  
  //Create the NDI sender using the description
  m_pNDI_send = NDIlib_send_create(&NDI_send_create_desc);

  /* open a client connection to the JACK server */
  jack_client = jack_client_open (c_name, options, &status, server_name);
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

  //initialize data structures for variable channels
  in_ports = (jack_port_t**)malloc(sizeof (jack_port_t*) * num_channels);
  size_t in_size = num_channels * sizeof(jack_default_audio_sample_t*);
  in = (jack_default_audio_sample_t**)malloc(in_size);

  /* create input JACK ports */
  for (int channel = 0; channel < num_channels; channel++){
   std::string channel_name_string = "input" + std::to_string(channel);
   //std::cout << "Current Channel Name: " << channel_name_string << std::endl;
   const char* channel_name_char = channel_name_string.c_str();
   printf("Creating JACK input port: %s, Channel: %d\n", channel_name_char, channel);
   in_ports[channel] = jack_port_register (jack_client, channel_name_char, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
   printf("Input JACK port created for %s\n", channel_name_char);
   if(in_ports[channel] == NULL){ //can't create JACK output ports - error
    fprintf(stderr, "no more JACK ports available\n");
    exit (1);
   }
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
  if(a_ports == true){ //make sure that auto connect of JACK ports is enabled
   ports = jack_get_ports (jack_client, NULL, NULL, JackPortIsPhysical|JackPortIsOutput);
   if(ports == NULL){
    fprintf(stderr, "no physical capture ports\n");
    exit (1);
   }

   if(jack_connect (jack_client, ports[0], jack_port_name (in_ports[0]))){
    fprintf(stderr, "cannot connect input ports\n");
   }

   if(jack_connect (jack_client, ports[1], jack_port_name (in_ports[1]))){
    fprintf (stderr, "cannot connect input ports\n");
   }

   jack_free (ports);
  }

  m_NDI_audio_frame.sample_rate = jack_sample_rate;
	m_NDI_audio_frame.no_channels = num_channels;
  audio_thread = std::thread(&send_audio::process_audio_thread, this); //start the audio processing in its own thread
}

// Destructor
send_audio::~send_audio(void){	// Wait for the thread to exit
	m_exit = true;
	jack_client_close(jack_client);
	// Destroy the sender thread
  audio_thread.join();
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
     break;  
    case 'j':
     client_name = optarg; 
     break;  
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
	 printf("JACK Client Name %s\n", client_name);
   printf("NDI Sender Name %s\n", ndi_name);
   if(auto_connect_jack_ports == true){
    printf("Auto Connect Ports\n");
   }else{
    printf("No Auto Connect Ports\n"); 
   }
   p_senders[0] = new send_audio(client_name,ndi_name,auto_connect_jack_ports);
                               
  /* keep running until the Ctrl+C */
  while(1){
   sleep(1);
  }
  
  exit (0);
}

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
#include <mongoose.h>
#include <thread>

#include <Processing.NDI.Lib.h>
#include <jack/jack.h>

int process_callback(jack_nframes_t x, void *p);

struct receive_audio {
 receive_audio(const NDIlib_source_t& source, const char *client_name="NDI_recv"); //constructor
 ~receive_audio(void); //destructor 
 public:
  int process(jack_nframes_t nframes);
 private:	
	NDIlib_recv_instance_t m_pNDI_recv; // Create the receiver
  NDIlib_framesync_instance_t m_pNDI_framesync; //NDI framesync
  jack_port_t *out_port1, *out_port2;
  jack_client_t *jack_client;
  std::thread m_receive_thread; // The thread to run
	std::atomic<bool> m_exit;	// Are we ready to exit
  void receive(void); // This is called to receive frames		
  static void jack_shutdown(void *arg); //This is called when JACK is shutdown
  
};

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data){
  if (ev == MG_EV_HTTP_MSG){
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;
   if(mg_http_match_uri(hm, "/ws")){ //upgrade to WebSocket
      mg_ws_upgrade(c, hm, NULL);
   }else if(mg_http_match_uri(hm, "/rest")) { //handle REST events
      mg_http_reply(c, 200, "", "{\"result\": %d}\n", 123);
   }else{ // Serve static files
      struct mg_http_serve_opts opts = {.root_dir = "."};
      mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
   }
  }else if (ev == MG_EV_WS_MSG){
    // Got websocket frame. Received data is wm->data. Echo it back!
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    mg_ws_send(c, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
  }
}

int receive_audio::process(jack_nframes_t nframes){
  jack_default_audio_sample_t *out1, *out2;
  //Get JACK Audio Buffers
  out1 = (jack_default_audio_sample_t*)jack_port_get_buffer (out_port1, nframes);
  out2 = (jack_default_audio_sample_t*)jack_port_get_buffer (out_port2, nframes);
  
  NDIlib_audio_frame_v2_t audio_frame;
  NDIlib_framesync_capture_audio(m_pNDI_framesync, &audio_frame, 44100, 0, nframes);
  //printf("Audio data received (%d samples).\n", audio_frame.no_samples);
  //std::cout << "Number of audio frames (JACK): " << nframes << std::endl;
  //std::cout << "Audio Frame Data (NDI): " << audio_frame.p_data << std::endl;
  //std::cout << "Channel Stride in Bytes (NDI): " << audio_frame.channel_stride_in_bytes << std::endl;
  //std::cout << "Size of Audio Frame (NDI): " << sizeof(audio_frame.p_data) << std::endl;
  //std::cout << "Number of Audio Channels (NDI): " << sizeof(audio_frame.no_channels) << std::endl;
  if(audio_frame.p_data != 0){ //make sure that there is data in the buffer before trying to copy anything
   float* p_ch1 = (float*)((uint8_t*)audio_frame.p_data + 0*audio_frame.channel_stride_in_bytes); //Get Channel 1 from NDI audio frame
   float* p_ch2 = (float*)((uint8_t*)audio_frame.p_data + 1*audio_frame.channel_stride_in_bytes); //Get Channel 2 from NDI audio frame
   memcpy(out1, p_ch1, sizeof(jack_default_audio_sample_t) * nframes); //copy the audio frame from NDI to the JACK buffer
   memcpy(out2, p_ch2, sizeof(jack_default_audio_sample_t) * nframes); //copy the audio frame from NDI to the JACK buffer
  }
  // Release the NDI audio frame. You could keep the frame if you want and release it later.
  NDIlib_framesync_free_audio(m_pNDI_framesync, &audio_frame);
  return 0;      
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void receive_audio::jack_shutdown(void *arg){
  exit(1);
}

//Constructor
receive_audio::receive_audio(const NDIlib_source_t& source, const char *client_name): m_pNDI_recv(NULL), m_exit(false), out_port1(NULL), out_port2(NULL){
  printf("Starting Receiver for %s\n", source.p_ndi_name);
  const char **ports;
  //const char *client_name;
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  NDIlib_recv_create_v3_t recv_create_desc;
  recv_create_desc.source_to_connect_to = source;
  recv_create_desc.bandwidth = NDIlib_recv_bandwidth_audio_only; //specify receiving audio frames only
  recv_create_desc.p_ndi_recv_name = "NDI Receiver";

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
   client_name = jack_get_client_name(jack_client);
   fprintf (stderr, "unique name `%s' assigned\n", client_name);
  }
  
  jack_set_process_callback (jack_client, ::process_callback, this); //This callback is called on every every time JACK does work - every audio sample
  jack_on_shutdown (jack_client, receive_audio::jack_shutdown, 0); //JACK shutdown callback - gets called on JACK shutdown
  
  /* create two output JACK ports */
  out_port1 = jack_port_register (jack_client, "output1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  out_port2 = jack_port_register (jack_client, "output2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  
  if((out_port1 == NULL) || (out_port2 == NULL)){ //can't create JACK output ports
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
  ports = jack_get_ports (jack_client, NULL, NULL, JackPortIsPhysical|JackPortIsInput);
  if(ports == NULL){
   fprintf(stderr, "no physical playback ports\n");
   exit (1);
  }

  if(jack_connect (jack_client, jack_port_name (out_port1), ports[0])){
   fprintf(stderr, "cannot connect output ports\n");
  }

  if(jack_connect (jack_client, jack_port_name (out_port2), ports[1])){
   fprintf (stderr, "cannot connect output ports\n");
  }

  jack_free (ports);

  // Create the receiver
	m_pNDI_recv = NDIlib_recv_create_v3(&recv_create_desc);
	assert(m_pNDI_recv);

  // Use a frame-syncronizer to ensure that the audio is dynamically resampled
  m_pNDI_framesync = NDIlib_framesync_create(m_pNDI_recv);
  // Start a thread to receive frames
	m_receive_thread = std::thread(&receive_audio::receive, this);
}

// Destructor
receive_audio::~receive_audio(void){	// Wait for the thread to exit
	m_exit = true;
	m_receive_thread.join();
	
	// Destroy the receiver
	NDIlib_recv_destroy(m_pNDI_recv);
}

void receive_audio::receive(void){
  while(!m_exit){
   sleep(1);
  }
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 */
int process_callback(jack_nframes_t x, void *p){
 return static_cast<receive_audio*>(p)->process(x); 
}

int main (int argc, char *argv[]){

  if (!NDIlib_initialize()){
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK documentation).
		// you can check this directly with a call to NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	
	// Create a finder
	NDIlib_find_create_t NDI_find_create_desc; /* Defalt settings */
	NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&NDI_find_create_desc);
	if (!pNDI_find) return 0;

	// Wait until there is one source
  uint32_t no_sources = 0;
  const NDIlib_source_t* p_sources = NULL;
  while (no_sources < 2){	// Wait until a source is found
   printf("Looking for sources ...\n");
   NDIlib_find_wait_for_sources(pNDI_find, 5000/* five seconds */);
   p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
  }

	// We need at least one source
	if (!p_sources) return 0;
  // Display all the sources.
  printf("NDI sources (%u found).\n", no_sources);
  for(uint32_t i = 0; i < no_sources; i++){
   printf("%u. %s\n", i + 1, p_sources[i].p_ndi_name);
  }
  
  //new receive_audio(p_sources[2]);
  new receive_audio(p_sources[0]);
  new receive_audio(p_sources[1]);
  // Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
  NDIlib_find_destroy(pNDI_find);
  
  struct mg_mgr mgr;                                
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "ws://0.0.0.0:80", fn, NULL);   // Create WebSocket and HTTP connection
  for (;;) mg_mgr_poll(&mgr, 1000);  // Block forever
  /* keep running until the Ctrl+C */
  while(1){
    printf("NDI sources (%s found).\n","Test");
   sleep(1);
  }
  
  exit (0);
}
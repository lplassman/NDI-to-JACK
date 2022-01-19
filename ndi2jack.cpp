/*
 * NDI Client to JACK (Jack Audio Connection Kit) Output
 * 
 * This program can be used and distrubuted without resrictions
 */

#include <cstdio>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <math.h>
#include <signal.h>
#include <ctime>
#include <unistd.h>
#include <mongoose.h>

#include <Processing.NDI.Lib.h>
#include <jack/jack.h>

jack_port_t *output_port1, *output_port2;
jack_client_t *jack_client;

NDIlib_recv_instance_t pNDI_recv;
NDIlib_framesync_instance_t pNDI_framesync;

static void signal_handler(int sig){
  jack_client_close(jack_client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

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

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 */

int process(jack_nframes_t nframes, void *arg){
  jack_default_audio_sample_t *out1, *out2;
  //Get JACK Audio Buffers
  out1 = (jack_default_audio_sample_t*)jack_port_get_buffer (output_port1, nframes);
  out2 = (jack_default_audio_sample_t*)jack_port_get_buffer (output_port2, nframes);
  
  NDIlib_audio_frame_v2_t audio_frame;
  NDIlib_framesync_capture_audio(pNDI_framesync, &audio_frame, 44100, 0, nframes);
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
  NDIlib_framesync_free_audio(pNDI_framesync, &audio_frame);
  return 0;      
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void jack_shutdown (void *arg){
  exit (1);
}

int main (int argc, char *argv[]){
  const char **ports;
  const char *client_name;
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  if(argc >= 2){		/* client name specified? */
		client_name = argv[1];
		if (argc >= 3) {	/* server name specified? */
			server_name = argv[2];
			int my_option = JackNullOption | JackServerName;
			options = (jack_options_t)my_option;
		}
	} else {			/* use basename of argv[0] */
		client_name = strrchr(argv[0], '/');
		if (client_name == 0) {
			client_name = argv[0];
		} else {
			client_name++;
		}
	}

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
  
  jack_set_process_callback (jack_client, process, nullptr); //This callback is called on every every time JACK does work - every audio sample
  jack_on_shutdown (jack_client, jack_shutdown, 0); //JACK shutdown callback - gets called on JACK shutdown
  
  /* create two output JACK ports */
  output_port1 = jack_port_register (jack_client, "output1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  output_port2 = jack_port_register (jack_client, "output2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  
  if((output_port1 == NULL) || (output_port2 == NULL)){ //can't create JACK output ports
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

  if(jack_connect (jack_client, jack_port_name (output_port1), ports[0])){
   fprintf(stderr, "cannot connect output ports\n");
  }

  if(jack_connect (jack_client, jack_port_name (output_port2), ports[1])){
   fprintf (stderr, "cannot connect output ports\n");
  }

  jack_free (ports);
    
  /* install a signal handler to properly stop jack client process */
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);

  //NDI Initialize
  if (!NDIlib_initialize()) return 0;
	
  // Create a NDI finder
  NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
  if (!pNDI_find) return 0;

  NDIlib_recv_create_v3_t NDI_recv_create_desc;
  NDI_recv_create_desc.bandwidth = NDIlib_recv_bandwidth_audio_only; //specify receiving audio frames only
  NDI_recv_create_desc.p_ndi_recv_name = "NDI Receiver";

  // Wait until there is one source
  uint32_t no_sources = 0;
  const NDIlib_source_t* p_sources = NULL;
  while (!no_sources){	// Wait until a source is found
   printf("Looking for sources ...\n");
   NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
   p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
  }
  // Display all the sources.
  printf("NDI sources (%u found).\n", no_sources);
  for(uint32_t i = 0; i < no_sources; i++){
   printf("%u. %s\n", i + 1, p_sources[i].p_ndi_name);
  }
  // We now have at least one source, so we create a receiver to look at it.
  pNDI_recv = NDIlib_recv_create_v3(&NDI_recv_create_desc);
  if (!pNDI_recv) return 0;
  
  // Connect to the first found source
  NDIlib_recv_connect(pNDI_recv, p_sources + 0);
  
  // Use a frame-syncronizer to ensure that the audio is dynamically resampled
  pNDI_framesync = NDIlib_framesync_create(pNDI_recv);

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
  // Free the NDI frame-sync
  NDIlib_framesync_destroy(pNDI_framesync);
  
  // Destroy the receiver
  NDIlib_recv_destroy(pNDI_recv);

  // Not required, but nice
  NDIlib_destroy();
	
  jack_client_close (jack_client);
  exit (0);
}

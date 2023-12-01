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
#include <mongoose.h>
#include "mjson.h"
#include <thread>
#include <chrono>

#include <getopt.h> 
#include <condition_variable>

#include <Processing.NDI.Lib.h>
#include <jack/jack.h>

NDIlib_find_create_t NDI_find_create_desc; /* Default settings for NDI find */
NDIlib_find_instance_t pNDI_find;
NDIlib_recv_instance_t pNDI_recv; //receiver for finding stream info
int stream_info[3]; //stream info for the NDI stream
const NDIlib_source_t* p_sources = NULL;
struct mg_mgr mgr;   
bool auto_connect_jack_ports = true;
float main_volume = 0.5f; //set to half volume by default

//Function Definitions
int process_callback(jack_nframes_t x, void *p);
std::string convertToString(char* a);
bool get_ndi_info(const char* source);



struct receive_audio {
 receive_audio(const char* source, const char *client_name="NDI_recv", int channel_count = 2); //constructor
 ~receive_audio(void); //destructor 
 public:
  int process(jack_nframes_t nframes);
 private:	
	NDIlib_recv_instance_t m_pNDI_recv; // Create the receiver
  NDIlib_framesync_instance_t m_pNDI_framesync; //NDI framesync
  NDIlib_audio_frame_v3_t audio_frame;
  jack_port_t **out_ports;
  jack_default_audio_sample_t *out;
  jack_default_audio_sample_t *p_ch;
  jack_client_t *jack_client;
  jack_nframes_t jack_sample_rate;
  float channel_volume = 1.0f; //set channel volume to full
  int num_channels = 2; //default number of channels
	std::atomic<bool> m_exit;	// Are we ready to exit	
  static void jack_shutdown(void *arg); //This is called when JACK is shutdown
};

int receive_audio::process(jack_nframes_t nframes){
  //Get JACK Audio Buffers
  NDIlib_framesync_capture_audio_v2(m_pNDI_framesync, &audio_frame, jack_sample_rate, num_channels, nframes);
  //printf("Audio data received (%d samples).\n", audio_frame.no_samples);
  //std::cout << "Number of audio frames (JACK): " << nframes << std::endl;
  //std::cout << "Audio Frame Data (NDI): " << audio_frame.p_data << std::endl;
  //std::cout << "Channel Stride in Bytes (NDI): " << audio_frame.channel_stride_in_bytes << std::endl;
  //std::cout << "Size of Audio Frame (NDI): " << sizeof(audio_frame.p_data) << std::endl;
  //std::cout << "Number of Audio Channels (NDI): " << sizeof(audio_frame.no_channels) << std::endl;
  for (int channel = 0; channel < num_channels; channel++){ //go through each channel
    out = (jack_default_audio_sample_t*)jack_port_get_buffer(out_ports[channel], nframes);
    p_ch = (jack_default_audio_sample_t*)(uint8_t *)(&audio_frame.p_data[channel * audio_frame.channel_stride_in_bytes]); //Get channels from NDI audio frame
    for (int sample_no = 0; sample_no < audio_frame.no_samples; sample_no++){ //apply the volume to each sample
      out[sample_no] = p_ch[sample_no] * main_volume * channel_volume; //copies the adjusted NDI framedata into the JACK buffer
    }
  }
  // Release the NDI audio frame. You could keep the frame if you want and release it later.
  NDIlib_framesync_free_audio_v2(m_pNDI_framesync, &audio_frame);
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
receive_audio::receive_audio(const char* source, const char *client_name, int channel_count): m_pNDI_recv(NULL), m_pNDI_framesync(NULL), m_exit(false), jack_client(NULL){
  printf("Starting Receiver for %s\n", source);
  const char **found_ports;
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  NDIlib_recv_create_v3_t recv_create_desc;
  recv_create_desc.source_to_connect_to = source;
  recv_create_desc.bandwidth = NDIlib_recv_bandwidth_audio_only; //specify receiving audio frames only
  recv_create_desc.p_ndi_recv_name = "NDI Receiver";
  num_channels = channel_count;

  /* open a client connection to the JACK server */
  fprintf (stderr, "Opening connection to JACK server...\n");
  jack_client = jack_client_open (client_name, options, &status, server_name);
  fprintf (stderr, "JACK server connection opened\n");
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

  jack_sample_rate = jack_get_sample_rate(jack_client);
  
  jack_set_process_callback (jack_client, ::process_callback, this); //This callback is called on every every time JACK does work - every audio sample
  jack_on_shutdown (jack_client, receive_audio::jack_shutdown, 0); //JACK shutdown callback - gets called on JACK shutdown
  
  //initialize data structures for variable channels
  out_ports = (jack_port_t**)malloc(sizeof (jack_port_t*) * num_channels);

  /* create output JACK ports */
  for (int channel = 0; channel < num_channels; channel++){
   std::string channel_name_string = "output_" + std::to_string(channel);
   //std::cout << "Current Channel Name: " << channel_name_string << std::endl;
   const char* channel_name_char = channel_name_string.c_str();
   printf("Creating JACK output port: %s, Channel: %d\n", channel_name_char, channel);
   out_ports[channel] = jack_port_register (jack_client, channel_name_char, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
   printf("Output JACK port created for %s\n", channel_name_char);
   if(out_ports[channel] == NULL){ //can't create JACK output ports - error
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
  if(auto_connect_jack_ports == true){ //make sure auto connect is enabled
   found_ports = jack_get_ports (jack_client, NULL, NULL, JackPortIsInput);
   if (found_ports) {
    int i;
		for (i = 0; found_ports[i]; ++i) {
			printf("name: %s\n", found_ports[i]);
		}
   }

   if(jack_connect (jack_client, jack_port_name (out_ports[0]), found_ports[0])){
    fprintf(stderr, "cannot connect output ports\n");
   }

   if(jack_connect (jack_client, jack_port_name (out_ports[1]), found_ports[1])){
    fprintf (stderr, "cannot connect output ports\n");
   }

   jack_free (found_ports);
  }

  // Create the receiver
	m_pNDI_recv = NDIlib_recv_create_v3(&recv_create_desc);
	assert(m_pNDI_recv);

  // Use a frame-synchronizer to ensure that the audio is dynamically resampled
  m_pNDI_framesync = NDIlib_framesync_create(m_pNDI_recv); //starts in its own thread
}

// Destructor
receive_audio::~receive_audio(void){	// Wait for the thread to exit
	m_exit = true;
	jack_client_close(jack_client);
	// Destroy the receiver
  NDIlib_framesync_destroy(m_pNDI_framesync);
	NDIlib_recv_destroy(m_pNDI_recv);
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 */
int process_callback(jack_nframes_t x, void *p){
 return static_cast<receive_audio*>(p)->process(x); 
}

static const int no_receivers = 30; //max number of receivers
receive_audio* p_receivers[no_receivers] = { 0 };
std::string ndi_running_name[no_receivers] = { "" }; //name of the connected NDI stream

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data){
  if(ev == MG_EV_WS_OPEN){
    c->label[0] = 'W';  // Mark this connection as an established WS client
  }
  if (ev == MG_EV_HTTP_MSG){
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;
  struct mg_connection *c2 = mgr.conns;
   if(mg_http_match_uri(hm, "/ws")){ //upgrade to WebSocket
      mg_ws_upgrade(c, hm, NULL);
   }else if(mg_http_match_uri(hm, "/rest")) { //handle REST events
      mg_http_reply(c, 200, "", "{\"result\": %d}\n", 123);
   }else{ // Serve static files
      struct mg_http_serve_opts opts = {.root_dir = "/opt/ndi2jack/assets/"};
      mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
   }
  }else if (ev == MG_EV_WS_MSG){
    // Got websocket frame. Received data is wm->data. Echo it back!
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    //std::cout << "WebSocket: " << wm->data.ptr << std::endl;
    char prefix_buf[100];
    char action_buf[100];
    mjson_get_string(wm->data.ptr, wm->data.len, "$.prefix", prefix_buf, sizeof(prefix_buf)); //get prefix
    mjson_get_string(wm->data.ptr, wm->data.len, "$.action", action_buf, sizeof(action_buf)); //get action
    std::string prefix_string = convertToString(prefix_buf);
    std::string action_string = convertToString(action_buf);
    //std::cout << "prefixString: " << prefix_string << std::endl;
    //std::cout << "actionString: " << action_string << std::endl;
    if(prefix_string == "refresh"){
     if(action_string == "refresh"){

      uint32_t no_sources = 0; 
      p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
      std::string discover_json;
      std::string source_json = "";
      discover_json = "{\"prefix\":\"discover_source\",\"action\":\"display\",\"source_list\":{";
      for(uint32_t i = 0; i < no_sources; i++){
       std::string ndi_string = p_sources[i].p_ndi_name;
       std::string url_string = p_sources[i].p_url_address;
       std::string source_id = std::to_string(i); 
       int conflict = 0;
       for(uint32_t i = 0; i < no_receivers; i++){ //check for conflicts
        if((ndi_running_name[i] == ndi_string)&&(conflict == 0)){
         conflict = 1; //found conflict with a name that is already stored - already running this receiver
        }  
       }
       if(conflict == 0){ //since this is not running on a receiver - display
        //std::cout << "Source IP: " << p_sources[i].p_url_address << std::endl;
        if(source_json == ""){
         source_json += "\""+source_id + "\":{\"name\":\""+ndi_string+"\",\"url\":\""+url_string+"\"}";  
        }else{
         source_json += ",\""+source_id + "\":{\"name\":\""+ndi_string+"\",\"url\":\""+url_string+"\"}";  
        }
       }
      }
      discover_json += source_json;
      discover_json += "}";
      discover_json += "}";
      const char* pub_json1 = discover_json.c_str();
      for (struct mg_connection *c2 = mgr.conns; c2 != NULL; c2 = c2->next) { //traverse over all client connections
       if (c2->label[0] == 'W'){ //make sure it is a websocket connection
        mg_ws_send(c2, pub_json1, strlen(pub_json1), WEBSOCKET_OP_TEXT);
       }
      }

      std::string connected_json;
      source_json = "";
      connected_json = "{\"prefix\":\"playing_source\",\"action\":\"display\",\"source_list\":{";
      for(uint32_t i = 0; i < no_receivers; i++){
       if(ndi_running_name[i] != ""){ //make sure receiver is not empty
        std::string source_id = std::to_string(i); 
        if(source_json == ""){
         source_json += "\""+source_id + "\":{\"name\":\""+ndi_running_name[i]+"\"}";  
        }else{
         source_json += ",\""+source_id + "\":{\"name\":\""+ndi_running_name[i]+"\"}";  
        }
       }
      }
      connected_json += source_json;
      connected_json += "}";
      connected_json += "}";
      const char* pub_json2 = connected_json.c_str();
      for (struct mg_connection *c2 = mgr.conns; c2 != NULL; c2 = c2->next) { //traverse over all client connections
       if (c2->label[0] == 'W'){ //make sure it is a websocket connection
        mg_ws_send(c2, pub_json2, strlen(pub_json2), WEBSOCKET_OP_TEXT);
       }
      }
     }
     if(action_string == "re_vol"){
      std::string volume_json;
      std::string source_json = "";
      volume_json = "{\"prefix\":\"update_volume\",\"action\":\"display\",\"volume_info\":{";
      std::string volume_level = std::to_string(main_volume);
      if(source_json == ""){
       source_json += "\"main_vol\":\""+volume_level+"\"";
      }
      
      volume_json += source_json;
      volume_json += "}";
      volume_json += "}";
      const char* pub_json3 = volume_json.c_str();
      for (struct mg_connection *c2 = mgr.conns; c2 != NULL; c2 = c2->next) { //traverse over all client connections
       if (c2->label[0] == 'W'){ //make sure it is a websocket connection
        mg_ws_send(c2, pub_json3, strlen(pub_json3), WEBSOCKET_OP_TEXT);
       }
      }
     } 
    }

    if(prefix_string == "connect_source"){
     int source_id = std::stoi(action_string);
     int stored = 0;
     int receiver_id = 0;
     int conflict = 0;
     std::string ndi_string = p_sources[source_id].p_ndi_name;
     for(uint32_t i = 0; i < no_receivers; i++){ //check for conflicts
      if((ndi_running_name[i] == ndi_string)&&(conflict == 0)){
       conflict = 1; //found conflict with a name that is already stored - already running this receiver
      }  
     }
     if(conflict == 0){
      for(uint32_t i = 0; i < no_receivers; i++){
       if(stored == 0){
        if(ndi_running_name[i] == ""){ //empty string array
         ndi_running_name[i] = ndi_string;
         std::cout << "ID: " << i << std::endl;
         stored = 1;
         receiver_id = i;
        } 
       }
      }
      get_ndi_info(p_sources[source_id].p_ndi_name);
      p_receivers[receiver_id] = new receive_audio(p_sources[source_id].p_ndi_name, "NDI_recv", stream_info[2]); 
     }else{
      //std::cout << "Receiver already running for:  " << p_sources[source_id].p_ndi_name << std::endl; 
     }
    }

    if(prefix_string == "disconnect_source"){ //remove a connected source
     int source_id = std::stoi(action_string);
     delete p_receivers[source_id]; //delete receiver
     ndi_running_name[source_id] = ""; //update the running receiver
    }

    if(prefix_string == "save_streams"){ //save the current connected streams
     std::ofstream preset_file("/opt/ndi2jack/assets/presets.txt");
     for(uint32_t i = 0; i < no_receivers; i++){
      if(ndi_running_name[i] != ""){ //make sure a receiver is stored before trying to save in file
      preset_file << ndi_running_name[i];
      preset_file << std::endl;
      }
     }
     preset_file.close();
    }

    if(prefix_string == "am"){ //adjust the main volume - all output channels are adjusted
     main_volume = std::stof(action_string); //get the float volume from the websocket and set the main_volume variable
    }
    
  }
}

bool get_ndi_info(const char* source){
  NDIlib_recv_create_v3_t recv_create_desc;
  recv_create_desc.source_to_connect_to = source;
  recv_create_desc.bandwidth = NDIlib_recv_bandwidth_audio_only; //specify receiving audio frames only
  recv_create_desc.p_ndi_recv_name = "NDI Info";
  pNDI_recv = NDIlib_recv_create_v3(&recv_create_desc); //create a receiver that connects to the source
	assert(pNDI_recv);
	NDIlib_audio_frame_v3_t audio_frame;
  printf("Getting NDI audio info for %s...\n", source);
  bool timeout = false;
  bool got_info = false;
  using namespace std::chrono;
  const auto start_time = high_resolution_clock::now(); //get start time
  while(timeout == false){
	  switch (NDIlib_recv_capture_v3(pNDI_recv, nullptr, &audio_frame, nullptr, 5000)){ //try to get data from the source
			// No data
			case NDIlib_frame_type_none:
				printf("No data received for NDI stream info.\n");
				break;

				// Audio data
			case NDIlib_frame_type_audio:
				printf("Samples (%d).\n", audio_frame.no_samples);
        printf("Sample Rate (%d).\n", audio_frame.sample_rate);
        printf("No Channels (%d).\n", audio_frame.no_channels);
        stream_info[0] = audio_frame.no_samples; //store the stream info in the array
        stream_info[1] = audio_frame.sample_rate;
        stream_info[2] = audio_frame.no_channels;
        timeout = true;
        got_info = true;
				NDIlib_recv_free_audio_v3(pNDI_recv, &audio_frame); //free the audio frame
				break;
		}
    if(high_resolution_clock::now() - start_time > seconds(5)){ //timeout after 5 seconds of no data
      timeout = true;
      printf("Timeout in getting NDI stream info.\n");
    }
  }
    // Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);
  return got_info;
}

static void usage(FILE *fp, int argc, char **argv){
        fprintf(fp,
                 "Usage: NDI to JACK [options]\n\n"
                 "Version 1.0\n"
                 "Options:\n"
                 "-h | --help          Print this message\n"
                 "-a | --auto-connect  Disable auto connect JACK ports (default to true)\n"
                 "",
                 argv[0]);
}

static const char short_options[] = "a";

static const struct option
long_options[] = {
        { "help",   no_argument,       NULL, 'h' },
        { "auto-connect", no_argument,       NULL, 'a' },
        { 0, 0, 0, 0 }
};

int main (int argc, char *argv[]){
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
  NDI_find_create_desc.show_local_sources = (bool)false; //don't include local sources when searching for NDI
	pNDI_find = NDIlib_find_create_v2(&NDI_find_create_desc);
	if (!pNDI_find) return 0; //error out if the NDI finder can't be created

  std::string output_text; //preset file is temporary stored in this variable
  std::ifstream preset_file("/opt/ndi2jack/assets/presets.txt"); //open the presets file
  while(getline(preset_file, output_text)){
   int stored = 0;
   int receiver_id = 0;
   const char* ndi_name = output_text.c_str();;
   std::string ndi_string = ndi_name;
   for(uint32_t i = 0; i < no_receivers; i++){
    if(stored == 0){
     if(ndi_running_name[i] == ""){ //empty string array - make sure it is empty before trying to start receiver
      ndi_running_name[i] = ndi_string;
      stored = 1;
      receiver_id = i;
     } 
    }
   }
   p_receivers[receiver_id] = new receive_audio(ndi_name, "NDI_recv", 2); //2 channels by default
  }
                               
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "ws://0.0.0.0:80", fn, NULL);   // Create WebSocket and HTTP connection
  for (;;) mg_mgr_poll(&mgr, 1000);  // Block forever
  /* keep running until the Ctrl+C */
  while(1){
   sleep(1);
  }
  
  exit (0);
}

std::string convertToString(char* a){
  std::string s = a;
  return s;
}
/*
 * Optitrack-Vicon-Lcm-Stub is used to acquire data streams from Optitrack
 * MOCAP system and to publish data in Vicon LCM format (vicon_t)
 * Optitrack-Vicon-Lcm-Stub is built around NatNetLinux, 
 * and is Copyright 2015-2020, Roberto Marino <formica@member.fsf.org>
 *
 * Optitrack-Vicon-Lcm-Stub is free software: 
 * you can redistribute it and/or modify it under the terms 
 * of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NatNetLinux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NatNetLinux.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sstream>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <NatNetLinux/NatNet.h>
#include <NatNetLinux/CommandListener.h>
#include <NatNetLinux/FrameListener.h>

#include <lcm/lcm-cpp.hpp>
#include <vicon_t/vicon_pos_t.hpp>
#include <cmath>

#include <boost/program_options.hpp>
#include <time.h>

class Globals
{
public:
   
   // Parameters read from the command line
   static uint32_t localAddress;
   static uint32_t serverAddress;
   
   // State of the main() thread.
   static bool run;
};
uint32_t Globals::localAddress = 0;
uint32_t Globals::serverAddress = 0;
bool Globals::run = false;

// End the program gracefully.
void terminate(int)
{
   // Tell the main() thread to close.
   Globals::run = false;
}

// Set the global addresses from the command line.
void readOpts( int argc, char* argv[] )
{
   namespace po = boost::program_options;
   
   po::options_description desc("ovls a.k.a. Optitrack-Vicon-Lcm-Stub by Roberto Marino\nOptions");
   desc.add_options()
      ("help", "Display help message")
      ("local-addr,l", po::value<std::string>(), "Local IPv4 address")
      ("server-addr,s", po::value<std::string>(), "Server IPv4 address")
   ;
   
   po::variables_map vm;
   po::store(po::parse_command_line(argc,argv,desc), vm);
   
   if(
      argc < 5 || vm.count("help") ||
      !vm.count("local-addr") ||
      !vm.count("server-addr")
   )
   {
      std::cout << desc << std::endl;
      exit(1);
   }
   
   Globals::localAddress = inet_addr( vm["local-addr"].as<std::string>().c_str() );
   Globals::serverAddress = inet_addr( vm["server-addr"].as<std::string>().c_str() );
}


lcm::LCM lcmobj;
vicon_t::vicon_pos_t mocap_data_single;
std::list<vicon_t::vicon_pos_t> mocap_data;
std::vector<RigidBody> RBodies;

// This thread loop just prints frames as they arrive.
void printFrames(FrameListener& frameListener)
{
   bool valid;
   MocapFrame frame;
   Globals::run = true;
   while(Globals::run)
   {
      
      if(!lcmobj.good())
	exit(1);	
	
      while( true )
      {
         // Try to get a new frame from the listener.
         MocapFrame frame(frameListener.pop(&valid).first);
	 RBodies = frame.rigidBodies();
         // Quit if the listener has no more frames.
	 if( !valid )
		 break;
	 int j = 1;

	 std::ostringstream ch;
	 std::string ch_name;
	
	 for(std::vector<RigidBody>::iterator it = RBodies.begin(); it != RBodies.end(); ++it)
	 {
		 vicon_t::vicon_pos_t temp;
		 Point3f euler;
		 Point3f loc = it->location();
		 Quaternion4f ori = it->orientation();
		 //Roll - phi
		 euler.x = atan2(2*(ori.qx*ori.qy + ori.qz*ori.qw),1-(2*(pow(ori.qy,2)+pow(ori.qz,2))));
		 //Pitch - theta
		 euler.y = asin(2*(ori.qx*ori.qz - ori.qw*ori.qy));
		 //Yaw - psi
		 euler.z = atan2(2*(ori.qx*ori.qw + ori.qy*ori.qz),1-(2*(pow(ori.qz,2)+pow(ori.qw,2))));
		 temp.q[0] = loc.x;
		 temp.q[1] = loc.y;
		 temp.q[2] = loc.z;
		 temp.q[3] = euler.x;
		 temp.q[4] = euler.y;
		 temp.q[5] = euler.z;
		 temp.timestamp = 0;

	         ch.str("");
		 ch_name.clear();
			
		 ch << "MOCAP#" << (j++);
		 ch_name = ch.str();
		 lcmobj.publish(ch_name,&temp);
	 }		

	
/*** TEST PART	
	 mocap_data_single.timestamp = 0;
	 mocap_data_single.q[0] = 0;
         mocap_data_single.q[1] = 0;
	 mocap_data_single.q[2] = 0;
	 mocap_data_single.q[3] = 0;
	 mocap_data_single.q[4] = 0;
	 mocap_data_single.q[5] = 0;

	 lcmobj.publish("MOCAP",&mocap_data_single);
***/
	 std::cout << frame << std::endl;
      }
      
      // Sleep for a little while to simulate work :)
      usleep(1000);
   }
}

// This thread loop collects inter-frame arrival statistics and prints a
// histogram at the end. You can plot the data by copying it to a file
// (say time.txt), and running gnuplot with the command:
//     gnuplot> plot 'time.txt' using 1:2 title 'Time Stats' with bars
void timeStats(FrameListener& frameListener, const float diffMin_ms = 0.5, const float diffMax_ms = 7.0, const int bins = 100)
{
   size_t hist[bins];
   float diff_ms;
   int bin;
   struct timespec current;
   struct timespec prev;
   struct timespec tmp;
   
   std::cout << std::endl << "Collecting inter-frame arrival statistics...press ctrl-c to finish." << std::endl;
   
   memset(hist, 0x00, sizeof(hist));
   bool valid;
   Globals::run = true;
   while(Globals::run)
   {
      while( true )
      {
         // Try to get a new frame from the listener.
         prev = current;
         tmp = frameListener.pop(&valid).second;
         // Quit if the listener has no more frames.
         if( !valid )
            break;
         
         current = tmp;
         
         diff_ms =
            std::abs(
               (static_cast<float>(current.tv_sec)-static_cast<float>(prev.tv_sec))*1000.f
                + (static_cast<float>(current.tv_nsec)-static_cast<float>(prev.tv_nsec))/1000000.f
            );
         
         bin = (diff_ms-diffMin_ms)/(diffMax_ms-diffMin_ms) * (bins+1);
         if( bin < 0 )
            bin = 0;
         else if( bin >= bins )
            bin = bins-1;
         
         hist[bin] += 1;
      }
      
      // Sleep for a little while to simulate work :)
      usleep(1000);
   }
   
   // Print the stats
   std::cout << std::endl << std::endl;
   std::cout << "# Time diff (ms), Count" << std::endl;
   for( bin = 0; bin < bins; ++bin )
      std::cout << diffMin_ms+(diffMax_ms-diffMin_ms)*(0.5f+bin)/bins << ", " << hist[bin] << std::endl;
}


int main(int argc, char* argv[])
{
   
   // Version number of the NatNet protocol, as reported by the server.
   unsigned char natNetMajor;
   unsigned char natNetMinor;
   
   // Sockets
   int sdCommand;
   int sdData;
   
   // Catch ctrl-c and terminate gracefully.
   signal(SIGINT, terminate);
   
   // Set addresses
   readOpts( argc, argv );
   // Use this socket address to send commands to the server.
   struct sockaddr_in serverCommands = NatNet::createAddress(Globals::serverAddress, NatNet::commandPort);
   
   // Create sockets
   sdCommand = NatNet::createCommandSocket( Globals::localAddress );
   sdData = NatNet::createDataSocket( Globals::localAddress );
   
   // Start the CommandListener in a new thread.
   CommandListener commandListener(sdCommand);
   commandListener.start();

   // Send a ping packet to the server so that it sends us the NatNet version
   // in its response to commandListener.
   NatNetPacket ping = NatNetPacket::pingPacket();
   ping.send(sdCommand, serverCommands);
   
   // Wait here for ping response to give us the NatNet version.
   commandListener.getNatNetVersion(natNetMajor, natNetMinor);
   
   // Start up a FrameListener in a new thread.
   FrameListener frameListener(sdData, natNetMajor, natNetMinor);
   frameListener.start();
   
   // This infinite loop simulates a "worker" thread that reads the frame
   // buffer each time through, and exits when ctrl-c is pressed.
   printFrames(frameListener);
   //timeStats(frameListener);
   
   // Wait for threads to finish.
   frameListener.stop();
   commandListener.stop();
   frameListener.join();
   commandListener.join();
   
   // Epilogue
   close(sdData);
   close(sdCommand);
   return 0;
}

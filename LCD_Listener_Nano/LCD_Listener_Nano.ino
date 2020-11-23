#include <EtherCard.h>
#include <LiquidCrystal.h>

/**
 * Xmutantson: So this code is heavily based on ModLogNet's code on github. https://github.com/ModLogNet/CDP-LLDP-with-arduino/blob/master/LCD_Listener_Nano.ino#L230
 * Most of the code that I wrote is heavy and slow but the CDP part works so far. The LLDP hasnt got LCD outputs programmed yet.
 * I couldnt figure out how to display VLAN information. Also if CDP and LLDP are on at the same time or if it recieves no data in some of the LCD_data,
 * then LCD_data[#] will contain old junk data. Some kind of LCD_data clearing needs to happen on each new packet rx. I tried to write them on each packet rx
 * but I dont know what I'm doing and I get the idea that the heap is getting screwed somehow. Speaking of heap, im using F() everywhere because otherwise there's not enough ram to go around.
 * also, with my test samples my packets arent larger than 400 bytes so the buffer could be turned down depending on how big the packets are. unfortunately we cant use the 
 * SRAM on the encj2860 because ethercard.h doesnt take it into account and we gotta load the whole packet in ram.
 * 
 * Improvements would be welcomed. Current SRAM usage is around 65% at compile time
*/


// ethernet interface mac address, must be unique on the LAN
byte cdp_mac[] = {
  0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc
};
byte mymac[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05
};

//-------------------NEW------------
byte lldp_mac[] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e};
int rbuflen;

//----------------------------------

const int rs = A5, en = A4, d4 = A0, d5 = A1, d6 = A2, d7 = A3; //set up 1602 to dodge the pins the ethernet shield is using by relocating to the other pin row >:(
LiquidCrystal lcd(rs, en, d4, d5, d6, d7); //for the *lcd.xyz();* commands later

unsigned long previousMillis = 0;        // will store last time LCD was updated. First thing in the state machine code
const long interval = 250;               // interval at which to update LCD (milliseconds)
unsigned long lcdCount = 0;              //depends on interval. increments when state machine content is called.
//creates delay multiplier for displaying successive lines of data in one run.
//eg when interval is 150, if lcdCount reaches 10 and something is waiting
//for it to be >= than that then it will wait 1.5 seconds and do something

int LCD_Window_control = 90; //this controls how many window cycles until you reset to the top, W1L1. controls window so as not to cycle through unused W#L#
//10 means only show the first window. 20 means show 1 and 2. 30 = 3 windows, etc. set to 9 windows
//window count currently active should be assertively limited by subroutines so if this remains 90 it's in error. I'll write a handler later

#define MAC_LENGTH 6
byte Ethernet::buffer[500];

int Device = 0;
int Port = 0;
int SWver = 0;
int Model = 0;
int CrntItem = 0;
int ValidPacket = 0;
String LCD_data[7];

char* ipTemplate = "";
char* macTemplate = "";

//messages per message window and line
//These are default values loaded when the program first initializes and if you see them after writing to the charstrings then you're doing something wrong.
char W1L1[17] = "DHCP IP:";            //WINDOWS 1 2 3 4 5 6 7 WITH LINE 1 2 BREAKOUTS
char W1L2[17] = "";                    //remember to use the leading space for the activity character and keep total msg to 16char
char W2L1[17] = "Gateway IP:";
char W2L2[17] = "";
char W3L1[17] = "DevName:";
char W3L2[17] = "";
char W4L1[17] = "SwDevMAC:";
char W4L2[17] = "";
char W5L1[17] = "Port:";
char W5L2[17] = "";                    //this one will be trimmed to final 16 digits
unsigned int sizeW5L2_Input = 0;       //this is needed to figure out the size of whats going into the chararray and to iterate through the last 16 digits
char W6L1[17] = "Model:";
char W6L2[17] = "";                    //this one will be trimmed to final 16 digits
unsigned int sizeW6L2_Input = 0;       //same function as above
char W7L1[17] = "VLAN:";
char W7L2[17] = "";
char W8L1[17] = "DevIP:";
char W8L2[17] = "";
char W9L1[17] = "VoiceVLAN:";
char W9L2[17] = "";

//be careful and watch the ram when you write more of these

#include <SPI.h>

//Use these pins for the ethernet shield!
#define sclk 13
#define mosi 11
#define cs   3
#define dc   8
#define rst  -1  // you can also connect this to the Arduino reset

void setup()
{
//speaking of ram usage, if you ever print something static you should use a flash string. Serial.print(F("something"));
  pinMode(13, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("Serial Monitor Initializing..."));
  //Serial.println(F("Initializing..."));
  //init lcd and display a starting sequence message. have to do it manually because the dynamic loop cant be called yet
  lcd.begin(16, 2);

  lcd.print(F("Arduino Ethernet"));
  lcd.setCursor(0, 1);
  lcd.print(F("CDP/LLDP reader "));
  delay(1500); //somewhat arbitrary, to allow you to actually read the first window
  lcd.setCursor(0, 0);
  lcd.print(F("Cred: Xmutantson"));
  lcd.setCursor(0, 1);
  lcd.print(F("&ModLogNetgithub"));
  delay(1000); 
  lcd.setCursor(0, 0);
  lcd.print(F("CDP Takes `60sec"));
  lcd.setCursor(0, 1);
  lcd.print(F("to be detected! "));
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(F("Waiting for IP  "));
  lcd.setCursor(0, 1);
  lcd.print(F("from DHCP server"));
  ether.begin(sizeof Ethernet::buffer, mymac, 10);

  if (!ether.dhcpSetup())
  {

    strcpy(W1L1, "DHCP failed.....");
    strcpy(W1L2, " Restart tool or");
    strcpy(W2L1, " Restart tool or");
    strcpy(W2L2, " check DHCP svr ");
    strcpy(W3L1, " check DHCP svr ");
    strcpy(W3L2, " or port wiring.");
    LCD_Window_control = 29;
    Serial.println(F("DHCP FailWHACK!"));
  }
  else
  {
    
    Serial.println(F("------START IP/Gateway Retrival-----"));
    //    testdrawtext("DHCP IP:",ST7735_GREEN);
    Serial.println(F("DHCP IP OBTAINED!"));  //serial print ack of getting dhcp working
    strcpy(W1L1, "My DHCP IP:     ");
    Serial.print(W1L1);
    ipTemplate = "%d.%d.%d.%d"; //this breaks the leading space rule because it has to to fit the full address
    sprintf(W1L2, ipTemplate, ether.myip[0],  ether.myip[1],  ether.myip[2],  ether.myip[3]); //convert the bytearray ether.myip to char and write to W1L2
    Serial.println(W1L2); //print what's going to this spot in the display window/line cycle

    delay(1000);
    ENC28J60::enablePromiscuous();

    // ether.printIp("GW: ", ether.gwip);
    Serial.println(F("Gateway IP OBTAINED!"));  //serial print ack of getting dhcp working
    strcpy(W2L1, "Gateway IP:     ");
    Serial.print(W2L1);
    ipTemplate = "%d.%d.%d.%d"; //this breaks the leading space rule because it has to to fit the full address
    sprintf(W2L2, ipTemplate, ether.gwip[0],  ether.gwip[1],  ether.gwip[2],  ether.gwip[3]); //convert the bytearray ether.myip to char and write to W1L2
    Serial.println(W2L2); //print what's going to this spot in the display window/line cycle
    
    Serial.println(F("------END IP/Gateway Retrival-----"));
    Serial.println(F(""));
  }
}
void loop()
{

  unsigned long currentMillis = millis();   //state machine guts. whatever is in the if will get run once per second
  if (currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    LCDupdate();                        //call lcdupdate
    if (lcdCount >= 10000) {            //watchdog to make sure lcdCount doesnt have unpredictable behavior after ~8 hours (when the long runs out)?? just tryin to be careful haha
      lcdCount = 0;
    }
    lcdCount++;                         //increment lcdCount to tell program how many times we've pulsed the lcd
    //Serial.println("lcdWHACK!");      //state machine indicator
    //Serial.println(lcdCount);         //lcdCount indicator
  }

  int plen = ether.packetReceive();
  if ( plen > 0 ) {

 
    if (byte_array_contains(Ethernet::buffer, 0, cdp_mac, sizeof(cdp_mac))) {

      //Protocal = "CDP";
      Serial.println(F("--------START Data Retrival-------"));
      Serial.println(F("    >>> CDP Packet Received"));
/**   this crashes the arduino
 //clear LCD data on new CDP packet rx
      LCD_data[0] = F("");
      LCD_data[1] = F("");
      LCD_data[2] = F("");
      LCD_data[3] = F("");
      LCD_data[4] = F("");
      LCD_data[5] = F("");
      LCD_data[6] = F("");
      LCD_data[7] = F("");
*/
      byte* macFrom = Ethernet::buffer + sizeof(cdp_mac);

      //Get source MAC address
      LCD_data[1] = print_mac(Ethernet::buffer, sizeof(cdp_mac), 6);

      int DataIndex = 26;

      while (DataIndex < plen) { // read all remaining TLV fields //checks to make sure packet is at least 27 long (28 bytes) before checking for details
        unsigned int cdpFieldType = (Ethernet::buffer[DataIndex] << 8) | Ethernet::buffer[DataIndex + 1];
        DataIndex += 2;
        unsigned int TLVFieldLength = (Ethernet::buffer[DataIndex] << 8) | Ethernet::buffer[DataIndex + 1];
        DataIndex += 2;
        TLVFieldLength -= 4;

        switch (cdpFieldType) {
          case 0x0001: //device
            Device = 1;
            handlePacketAsciiField(Ethernet::buffer, DataIndex, TLVFieldLength);
            Device = 0;
            break;
          case 0x0002: //mac
            handleCdpAddresses(Ethernet::buffer, DataIndex, TLVFieldLength); //doesnt work well because the CDP packets i have in the capture are anonymized
            break;
          case 0x0003: //port
            Port = 1;
            handlePacketAsciiField(Ethernet::buffer, DataIndex, TLVFieldLength);
            Port = 0;
            break;
//          case 0x0004:
//            handleCdpCapabilities(Ethernet::buffer, DataIndex, TLVFieldLength);
//            break;
          case 0x0006: //model
            Model = 1;
            handlePacketAsciiField(Ethernet::buffer, DataIndex, TLVFieldLength);
            Model = 0;
            break;
          case 0x000a: //vlan #
            handlePacketNumField(Ethernet::buffer, DataIndex, TLVFieldLength);
            break;

          case 0x000e: //CDP VLAN voice#
            handleCdpVoiceVLAN( Ethernet::buffer, DataIndex + 2, TLVFieldLength - 2);
            break;
        }
        DataIndex += TLVFieldLength;
      }
      Serial.println(F("--------END Data Retrival-------"));
      Serial.println(F(""));            
      
      Serial.println(F("--------START LCD Data Dump-------"));
      //Serial.println(F("LCD_data dump"));
      Serial.println(LCD_data[0]); //Device Name W3
      Serial.println(LCD_data[1]); //MAC Address W4
      Serial.println(LCD_data[2]); //Port        W5
      Serial.println(LCD_data[3]); //Model       W6
      Serial.println(LCD_data[4]); //vlan        W7
      Serial.println(LCD_data[5]); //IP          W8
      Serial.println(LCD_data[6]); //Voice VLAN  W9
      //Serial.println(F("dump complete"));        
      Serial.println(F("--------END LCD Data Dump-------"));
      Serial.println(F(""));      
      
      Serial.println(F("--------START LCD Write-------"));
      Serial.println(F("Writing CDP Data to display windows"));
      
      
      strcpy(W3L1, "CDPDevID:       ");  //LCD_data[0] Name
      strcpy(W3L2, LCD_data[0].c_str()); //LCD_data[0] Data
      Serial.print(F("Window 3 (CDPDevID)  Written to LCD: "));
      Serial.println(W3L2);
   
      strcpy(W4L1, "CDPDevMAC:      "); //1 space
      strcpy(W4L2, (LCD_data[1].c_str()));
      Serial.print(F("Window 4 (CDPDevMAC  Written to LCD: "));
      Serial.println(W4L2);
      
      sizeW5L2_Input = (LCD_data[2].length());
      strcpy(W5L1, "Port ID: (trim)  "); //LCD_data[2] Name
      strcpy(W5L2, (LCD_data[2].substring(sizeW5L2_Input - 16, sizeW5L2_Input)).c_str()); //LCD_data[2] Data
      Serial.print(F("Window 5 (Port ID)   Written to LCD: "));
      Serial.println(W5L2);

      sizeW6L2_Input = (LCD_data[3].length());
      strcpy(W6L1, "Model No: (trim) "); //LCD_data[3] Name
      strcpy(W6L2, (LCD_data[3].substring(sizeW6L2_Input - 16, sizeW6L2_Input)).c_str()); //LCD_data[3] Data
      Serial.print(F("Window 6 (Model)     Written to LCD: "));
      Serial.println(W6L2);
      
      strcpy(W7L1, "VLAN ID:        ");  //LCD_data[4] Name
      strcpy(W7L2, LCD_data[4].c_str()); //LCD_data[4] Data
      //splitDevMAC(); //call parse and display device mac routine. uses W4
      Serial.print(F("Window 7 (VLAN ID)   Written to LCD: "));
      Serial.println(W7L2);

      strcpy(W8L1, "CDPDevIP:       ");  //LCD_data[5] Name
      strcpy(W8L2, LCD_data[5].c_str()); //LCD_data[5] Data
      Serial.print(F("Window 8 (CDPDevIP)  Written to LCD: "));
      Serial.println(W8L2);

      strcpy(W9L1, "Voice VLAN:     ");  //LCD_data[6] Name
      strcpy(W9L2, LCD_data[6].c_str()); //LCD_data[6] Data
      Serial.print(F("Window 9 (VoiceVLAN) Written to LCD: "));
      Serial.println(W9L2);

      //update the display window!
      LCD_Window_control = 90;
      Serial.println(F("--------END LCD Write-------"));
      Serial.println(F(""));
    }

    
     
    //LLDP STUFF NOW
//    if (byte_array_contains(Ethernet::buffer, 0, lldp_mac, sizeof(lldp_mac))) {
//      
        //LLDP Packet found and is now getting processed
        //Protocal = "LLDP";
//      Serial.println(F("LLDP Packet Received"));

  /* this crashes the arduino
        //clear LCD data on new LLDP packet rx
        LCD_data[0] = F("");
        LCD_data[1] = F("");
        LCD_data[2] = F("");
        LCD_data[3] = F("");
        LCD_data[4] = F("");
        LCD_data[5] = F("");
        LCD_data[6] = F("");
        LCD_data[7] = F("");
  */      
//      LCD_data[1] = print_mac(Ethernet::buffer, sizeof(lldp_mac), 6);
//
//      int DataIndex = 14;
//
//      while (DataIndex < plen) { // read all remaining TLV fields
//        unsigned int lldpFieldType = (Ethernet::buffer[DataIndex]);
//        DataIndex += 1;
//        unsigned int TLVFieldLength = (Ethernet::buffer[DataIndex]);
//        Serial.print(" type:");
//          Serial.print(lldpFieldType, HEX);
//          Serial.print(" Length:");
//          Serial.print(TLVFieldLength); //for debugging
//        DataIndex += 1;
//
//        switch (lldpFieldType) {
//
//          case 0x0004: //Port Name/interface. such as switch0 or eth2
//            Port = 1;
//            handlePacketAsciiField(Ethernet::buffer, DataIndex + 1, TLVFieldLength - 1);
//            Port = 0;
//            break;
//
//          case 0x0006: //TTL
//
//            break;
//
//          case 0x0008: //Port Description
//
//            break;
//
//          case 0x000a: //Device ID
//            Device = 1;
//            handlePacketAsciiField(Ethernet::buffer, DataIndex+2, (TLVFieldLength));
//            Device = 0;
//            break;
//
//          case 0x000e://Capabilities
//            // handleCdpCapabilities( Ethernet::buffer, DataIndex + 2, TLVFieldLength - 2);
//            break;
//
//          case 0x0010: //Management IP Address
//            handleLLDPIPField(Ethernet::buffer, DataIndex + 1, TLVFieldLength);
//            break;
//
//          case 0x00fe: //LLDP Organisational TLV #
//            handleLLDPOrgTLV(Ethernet::buffer, DataIndex, TLVFieldLength);
//            break;
//        }
//        DataIndex += TLVFieldLength;
//      }
//      //update the W#L# here
//      Serial.println(F("LCD_data dump"));
//      Serial.println(LCD_data[0]);
//      Serial.println(LCD_data[1]); //mac address
//      Serial.println(LCD_data[2]);
//      Serial.println(LCD_data[3]);
//      Serial.println(LCD_data[4]); //vlan
//      Serial.println(LCD_data[5]);
//      Serial.println(LCD_data[6]);
//      Serial.println(F("dump complete"));
//      //to be clear: WE HAVEN'T WRITTEN CODE TO UPDATE THE LCD YET. All the LLDP handler does is print to the serial monitor
//      }
  }
}

void handleLLDPOrgTLV( const byte a[], unsigned int offset, unsigned int lengtha) {
  unsigned int Orgtemp;
  Serial.println(F(" LLDP ORG TLV handler called"));
  // for (unsigned int i = offset; i < ( offset + 3 ); ++i ) {
  //   Orgtemp += a[i];
  // }
  Orgtemp = (a[offset] << 16) | (a[offset + 1] << 8) | a[offset + 2];
  //  Serial.println( Orgtemp, HEX);
  /*Serial.print("\n Org Type:");
    Serial.print(Orgtemp, HEX);
    Serial.print(" Length:");
    Serial.print(lengtha);*/
  switch (Orgtemp) {

    case 0x0012BB:
      //TIA TR-41
      /*  Serial.print("\n Subtype:");
        Serial.print(a[offset + 3], HEX);
        Serial.print(" Length:");
        Serial.print(lengtha);*/
      switch (a[offset + 3]) {

        /*case 0x0001:
          //TIA TR-41 - Media Capabilities - returned binary for LLDP-MED TLV's enabled - Ignored for now
          break; */

        case 0x0002:
          //TIA TR-41 -network policy - deals with Application types and additional settings - good for voice vlan, etc..
          /*          Serial.print("\n Subtype:");
                    Serial.print(a[offset + 3], HEX);
                    Serial.print(" Length:");
                    Serial.print(lengtha); */
          switch (a[offset + 4]) {
            case 0x0001: //TIA TR-41 - network policy - Voice
              unsigned int VoiceVlanID;
              VoiceVlanID = (a[offset + 5] << 16) | (a[offset + 6] << 8) | a[offset + 7];
              VoiceVlanID = VoiceVlanID >> 9; //shift the bits to the left to remove the first bits
              LCD_data[6] = String(VoiceVlanID, DEC);
              break;

            case 0x0002:  //TIA TR-41 - Network policy - Voice signaling

              break;
          }

          break;

        /*case 0x0003:
          //TIA TR-41 - Location Indentification
          break; */

        /*case 0x0004:
          //TIA TR-41 - Extended power-via-MDI - POE information - returns a byte array
          break; */

        /*case 0x0005:
          //TIA TR-41 - Inventory - hardware revision
          break; */

        /*case 0x0006:
          //TIA TR-41 - Inventory - firmware revision
          break; */


        /*case 0x0007:
          //TIA TR-41 - Inventory - software revision
          break; */

        /*case 0x0009:
          //TIA TR-41 - Inventory - Manufacturer
          break;*/

        case 0x000a:
          //TIA TR-41 - Inventory - Model Name
          Model = 1;
          handlePacketAsciiField( a, offset + 4, lengtha - 4);
          Model = 0;
          break;

          /* case 0x000b:
            //TIA TR-41 - Inventory - Asset ID
            Model = 1;
            handlePacketAsciiField( a, offset + 4, lengtha - 4);
            Model = 0;
            break; */

      }
    case 0x00120F:
      //IEEE 802.3
      /* Serial.print("\n Subtype:");
        Serial.print(a[offset + 3], HEX);
        Serial.print(" Length:");
        Serial.print(lengtha);*/
      switch (a[offset + 3]) {
          //case 0x0001:
          //IEEE 802.3 - IEEE MAC/PHY Configuration/Status\n");
          //returns HEX array for different port settings like autonegotiate and speed.
          //break;
      }
      break;
    case 0x0080C2:
      //IEEE

      switch (a[offset + 3]) {
        case 0x0001:
          //IEEE - Port VLAN ID
          Serial.println(F("IEEE IEEEE IEEE"));
          //the next octets are the vlan #
          handlePacketNumField( a, offset + 4, 2);
          break;
      }
      break;
  }

}

//formerly cdp instead of packet
void handlePacketAsciiField(byte a[], unsigned int offset, unsigned int length) {
  print_str(a, offset, length);
  Serial.println(F("    >>> packetascii handler called"));
}

//formerly cdp instead of packet //planned
void handlePacketNumField(const byte a[], unsigned int offset, unsigned int length) {
  unsigned long num = 0;
  for (unsigned int i = 0; i < length; ++i) {
    num <<= 8;
    num += a[offset + i];
  }


  //Serial.print(num, DEC);
  //lcd.setCursor(0,1);
  // lcd.print();
  //lcd.setCursor(5,1);
  //lcd.print(num);
  //Serial.println();
  LCD_data[4] = "" + String(num, DEC);

}

//dont change this one. This is specific to CDP

void handleCdpAddresses(const byte a[], unsigned int offset, unsigned int length) {
  //Serial.println(F("Addresses: "));
//Serial.println(F("handleCdpAddresses called"));
  unsigned long numOfAddrs = (a[offset] << 24) | (a[offset + 1] << 16) | (a[offset + 2] << 8) | a[offset + 3];
  offset += 4;

  for (unsigned long i = 0; i < numOfAddrs; ++i) {
    unsigned int protoType = a[offset++];
    unsigned int protoLength = a[offset++];
    byte proto[8];
    for (unsigned int j = 0; j < protoLength; ++j) {
      proto[j] = a[offset++];
    }
    unsigned int addressLength = (a[offset] << 8) | a[offset + 1];

    offset += 2;
    byte address[4];
    if (addressLength != 4) Serial.println(F("Expecting address length: 4"));

    LCD_data[5] = "";
    for (unsigned int j = 0; j < addressLength; ++j) {
      address[j] = a[offset++];

      LCD_data[5] = LCD_data[5] + address[j] ;
      if (j < 3) {
        LCD_data[5] = LCD_data[5] + ".";
      }
      Serial.print(address[j] + ".");
    }
    //  uint8_t ipaddr[4];
    //ether.parseIp(ipaddr, LCD_data[5]);


  }

}

void handleCdpVoiceVLAN( const byte a[], unsigned int offset, unsigned int length) {
  unsigned long num = 0;
  for (unsigned int i = offset; i < ( offset + length ); ++i) {
    num <<= 8;
    // Serial.print(a[i]);
    num += a[i];
  }
  LCD_data[6] = String(num, DEC);
}


  void handleLLDPIPField(const byte a[], unsigned int offset, unsigned int lengtha) {
  Serial.println(F(" LLDPIP handler called"));
  int j = 0;
  LCD_data[5] = "";
  unsigned int AddressType = a[offset];
  switch (AddressType) {
    case 0x0001: //IPv4
      for (unsigned int i = offset + 1; i < ( offset + 1 + 4 ); ++i , ++j) {
        //LCD_data[5] = "";
        LCD_data[5] += a[i], DEC;
        if (j < 3) {
          LCD_data[5] += ".";
        }
      }
      break;

    case 0x0006: //MAC address?
      LCD_data[5] = print_mac (a, offset + 1,  6);
      break;
  }


  // int lengthostring = sizeof(LCD_data[5]);
  // LCD_data[5][lengtha] = '\0';
  // LCD_data[5] += '\0';
  }

//dont disable this or half the useful info will be gone
void print_str(byte a[], unsigned int offset, unsigned int length) {
  int j = 0;
  char temp [40];


  for (unsigned int i = offset; i < offset + 40; ++i , ++j) {
    if (Device != 0) {

      temp[j] = a[i];
      //lcd.print(temp);
      LCD_data[0] = temp;
    }

    if (Port != 0) {
      temp[j] = a[i];
      //lcd.print(temp);
      LCD_data[2] = temp;
      ;

    }

    if (Model != 0) {
      temp[j] = a[i];
      //lcd.print(temp);
      LCD_data[3] = temp;
      ;
    }
    //Serial.write(a[i]);
    //    return a[i];
  }
  /**
    Serial.println(F("Debug printouts for LCD_data 0 2 3"));
    Serial.println(LCD_data[0]);
    Serial.println(LCD_data[2]);
    Serial.println(LCD_data[3]);
    Serial.println(F("Debug printout finished"));
  */
}



/**
  String print_ip(const byte a[], unsigned int offset, unsigned int length) { //is now an unused function, was used for TFT display but rewrote
  String ip;
  for(unsigned int i=offset; i<offset+length; ++i) {
    //    if(i>offset) Serial.print('.');
    //   Serial.print(a[i], DEC);
    if(i>offset) ip = ip + '.';
    ip = ip + String (a[i]);
  }
  int iplentgh;
  ip=ip.substring(ip.length()-1)='\0';


  Serial.print(ip);
  return ip;
  }
*/
//NOT arduino mac but origin mac address. dont delete this. it's handy.
String print_mac(const byte a[], unsigned int offset, unsigned int length) {
  String Mac;
  char temp [13];
  LCD_data[1] = "";
  for (unsigned int i = offset; i < offset + length; ++i) {
    // if(i>offset) Serial.print(':');
    //  if(a[i] < 0x10) Serial.print('0');
    // Serial.print(a[i], HEX);

    if (i > offset) {
      //  LCD_data[1] = LCD_data[1] + Mac + ':';
      //Mac = Mac + ':'; //normally where you add the colon : which is removed for testing
    }
    if (a[i] < 0x10) {
      Mac = Mac + '0';
      //    LCD_data[1] = LCD_data[1] + Mac + '0';
    }
    Mac = Mac + String (a[i], HEX);
  }
  LCD_data[1] = LCD_data[1]  + Mac;
  //Serial.println(Mac);

  return Mac;
}



bool byte_array_contains(const byte a[], unsigned int offset, const byte b[], unsigned int length) {

  for (unsigned int i = offset, j = 0; j < length; ++i, ++j) {
    if (a[i] != b[j]) {

      return false;
    }

  }

  return true;
}


void LCDupdate () {  //updates display, sequentially. I know it's bad but this works for now until we need it to be better

  if (0 < lcdCount && lcdCount <= 10) {
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W1L1); //WINDOW 1 LINE 1
    lcd.setCursor(0, 1);
    lcd.print(W1L2); //WINDOW 1 LINE 2
  }
  if (10 < lcdCount && lcdCount <= 20) {
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W2L1);
    lcd.setCursor(0, 1);
    lcd.print(W2L2);
  }
  if (20 < lcdCount && lcdCount <= 30) {
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W3L1);
    lcd.setCursor(0, 1);
    lcd.print(W3L2);
  }
  if (30 < lcdCount && lcdCount <= 40) {
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W4L1);
    lcd.setCursor(0, 1);
    lcd.print(W4L2);
  }
  if (40 < lcdCount && lcdCount <= 50) {
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W5L1);
    lcd.setCursor(0, 1);
    lcd.print(W5L2);
  }
  if (50 < lcdCount && lcdCount <= 60) {
    //lcd.noAutoscroll();
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W6L1);
    lcd.setCursor(0, 1);
    lcd.print(W6L2);
  }
  if (60 < lcdCount && lcdCount <= 70) {
    //lcd.autoscroll();
    lcd.setCursor(0, 0);
    lcd.print(F("                ")); //WINDOW 1 LINE 1 CLEARING
    lcd.setCursor(0, 1);
    lcd.print(F("                ")); //WINDOW 1 LINE 2 CLEARING
    lcd.setCursor(0, 0);
    lcd.print(W7L1);
    lcd.setCursor(0, 1);
    lcd.print(W7L2);
  }

    if (70 < lcdCount && lcdCount <= 80) {
      //lcd.autoscroll();
      lcd.setCursor(0, 0);
      lcd.print("                "); //WINDOW 1 LINE 1 CLEARING
      lcd.setCursor(0, 1);
      lcd.print("                "); //WINDOW 1 LINE 2 CLEARING
      lcd.setCursor(0, 0);
      lcd.print(W8L1);
      lcd.setCursor(0, 1);
      lcd.print(W8L2);
    }
    if (80 < lcdCount && lcdCount <= 90) {
      //lcd.autoscroll();
      lcd.setCursor(0, 0);
      lcd.print("                "); //WINDOW 1 LINE 1 CLEARING
      lcd.setCursor(0, 1);
      lcd.print("                "); //WINDOW 1 LINE 2 CLEARING
      lcd.setCursor(0, 0);
      lcd.print(W9L1);
      lcd.setCursor(0, 1);
      lcd.print(W9L2);
    }

  if (lcdCount > LCD_Window_control ) { //reset scrollers
    lcdCount = 0;
  }
  /** activity light. not needed
    if (lcdCount & 1 == 0) { //if lcdCount is even
    lcd.setCursor(0, 13);
    lcd.print(" ");
    }
    if (lcdCount & 1 == 1) { //if lcdCount is odd
    lcd.setCursor(0, 13);
    lcd.print("*");
    }
  */
}

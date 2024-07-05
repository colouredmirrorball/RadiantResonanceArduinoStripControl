/*
  Original code by Josh Levine 2014
  https://github.com/bigjosh/SimpleNeoPixelDemo
  https://wp.josh.com/2014/05/13/ws2812-neopixels-are-not-so-finicky-once-you-get-to-know-them/

  Modified by CMB 2018:
  Added serial communication
  https://text2laser.be/blog/?p=79
*/

// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)

#define PIXELS 240  // Number of pixels in the string

// These values depend on which pin your string is connected to and what board you are using
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// These values are for the pin that connects to the Data Input pin on the LED strip. They correspond to...

// Arduino Yun:     Digital Pin 8
// DueMilinove/UNO: Digital Pin 12
// Arduino MeagL    PWM Pin 4

// You'll need to look up the port/bit combination for other boards.

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to
#define PIXEL_BIT   7      // Bit of the pin the pixels are connected to

//                PIXEL UPDATE CODE

// These are the timing constraints taken mostly from the WS2812 datasheets
// These are chosen to be conservative and avoid problems rather than for maximum throughput

#define T1H  900    // Width of a 1 bit in ns
#define T1L  600    // Width of a 1 bit in ns

#define T0H  400    // Width of a 0 bit in ns
#define T0L  900    // Width of a 0 bit in ns

#define RES 6000    // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )

// Actually send a bit to the string. We must to drop to asm to enusre that the complier does
// not reorder things and make it so the delay happens in the wrong place.

inline void sendBit( bool bitVal ) {

  if (  bitVal ) {        // 0 bit

    asm volatile (
      "sbi %[port], %[bit] \n\t"        // Set the output bit
      ".rept %[onCycles] \n\t"                                // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      "cbi %[port], %[bit] \n\t"                              // Clear the output bit
      ".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [bit]   "I" (PIXEL_BIT),
      [onCycles]  "I" (NS_TO_CYCLES(T1H) - 2),    // 1-bit width less overhead  for the actual bit setting, note that this delay could be longer and everything would still work
      [offCycles]   "I" (NS_TO_CYCLES(T1L) - 2)     // Minimum interbit delay. Note that we probably don't need this at all since the loop overhead will be enough, but here for correctness

    );

  } else {          // 1 bit

    // **************************************************************************
    // This line is really the only tight goldilocks timing in the whole program!
    // **************************************************************************


    asm volatile (
      "sbi %[port], %[bit] \n\t"        // Set the output bit
      ".rept %[onCycles] \n\t"        // Now timing actually matters. The 0-bit must be long enough to be detected but not too long or it will be a 1-bit
      "nop \n\t"                                              // Execute NOPs to delay exactly the specified number of cycles
      ".endr \n\t"
      "cbi %[port], %[bit] \n\t"                              // Clear the output bit
      ".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
      "nop \n\t"
      ".endr \n\t"
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [bit]   "I" (PIXEL_BIT),
      [onCycles]  "I" (NS_TO_CYCLES(T0H) - 2),
      [offCycles] "I" (NS_TO_CYCLES(T0L) - 2)

    );

  }

  // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the 5us reset timeout (which is A long time)
  // Here I have been generous and not tried to squeeze the gap tight but instead erred on the side of lots of extra time.
  // This has thenice side effect of avoid glitches on very long strings becuase


}


inline void sendByte( unsigned char byte ) {

  for ( unsigned char bit = 0 ; bit < 8 ; bit++ ) {

    sendBit( bitRead( byte , 7 ) );                // Neopixel wants bit in highest-to-lowest order
    // so send highest bit (bit #7 in an 8-bit byte since they start at 0)
    byte <<= 1;                                    // and then shift left so bit 6 moves into 7, 5 moves into 6, etc

  }
}

/*
  The following three functions are the public API:

  ledSetup() - set up the pin that is connected to the string. Call once at the begining of the program.
  sendPixel( r g , b ) - send a single pixel to the string. Call this once for each pixel in a frame.
  show() - show the recently sent pixel on the LEDs . Call once per frame.

*/


// Set the specified pin up as digital out

void ledsetup() {

  bitSet( PIXEL_DDR , PIXEL_BIT );

}

unsigned char stripint = 255;

inline void sendPixel( unsigned char r, unsigned char g , unsigned char b, unsigned char w )  {

  sendByte(g * stripint / 255);      // Neopixel wants colors in green then red then blue order
  sendByte(r * stripint / 255);
  sendByte(b * stripint / 255);
  sendByte(w * stripint / 255);

}


// Just wait long enough without sending any bits to cause the pixels to latch and display the last sent frame

void show() {
  _delay_us( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}

//    ------------------------    SETUP   ------------------------

void setup() {
  //Initialise the API
  ledsetup();

  //Make the strip red to display the arduino is ready (optional)
  showColor(255, 0, 0, 0);

  //Begin serial communication
  Serial.begin(115200);

  //Wait for the serial communication to commence
  while (!Serial)
  {

  }
  Serial.println("Ready Freddy");
}

//The program variable keeps track of which effect is currently enabled
unsigned char program = 'c';

//The current colour parameters (red, green, blue, white)
unsigned char data1 = 255, data2 = 0, data3 = 0, data4 = 0;

//Timing variables
long prevTime = 0;
long prevTimeProgram = 0;
boolean on = true;

//Lets us keep track of program changes
unsigned char prevProgram = 'c';

//Reusable counter variable
long counter = 0;

//Selectable strobe frequency
unsigned char value = 0;

//        ------------------------    LOOP    ----------------------------

void loop()
{

  //Don't go too fast: only update every 10 ms
  //This should be plenty of time for a serial message to come through
  if (millis() - prevTime > 10)
  {

    //We're now going to update LED strips, let the computer know!
    Serial.write('u');  //start sending LEDs, so don't you dare sending data now! very Unsafe!
    Serial.write(0);

    //Pick the currently enabled program
    switch (program)
    {

      case 'c':   //static colour

        showColor(data1, data2, data3, data4);

        break;

      case 's':   //strobo

        if (millis() - prevTimeProgram > 1000 / value)
        {
          on = !on;
          prevTimeProgram = millis();
        }
        if (on) showColor(data1, data2, data3, data4);
        else showColor(0, 0, 0, 0);

        break;

      case 'r':   //rainbows!

        rainbowStep();

        break;

        //Add your own effects here!

      default:
        showColor(0, 0, 0, 0);
        break;
    }

    //Reset timer
    prevTime = millis();
    
    while (Serial.available()) Serial.read(); //anything that's in the serial buffer now is junk!!!
    
    Serial.write('s');  //It's now Safe to send data to Arduino
    Serial.write(0);
  }

}


//          ----------------          SERIAL EVENT    ----------------

// If a serial message is received, this code will execute

void serialEvent()
{
  //Discard messages that are too short
  if (Serial.available() > 7)
  {
    
    //Start parsing the incoming message


    //First byte: a check character, should always be 'm'
    unsigned char checkM = Serial.read();
    if (checkM != 'm')
    {
      //We lost track...

      //Let the computer know
      Serial.write('e');
      Serial.write(0);
      Serial.flush();

      //If there is anything left in the Serial buffer, we're no longer interested in it
      while (Serial.available() > 0) Serial.read(); 
      return;

    }

    //Second byte: the program byte - can be 'c', 'r', 's', 'i', ...
    unsigned char command = Serial.read() & 0xff;

    //Third byte: value - strobo frequency, rainbow scroll speed, etc.
    value = Serial.read() & 0xff;

    //Fourth - seventh bytes: data (mainly RGBW values)
    data1 = Serial.read() & 0xff;
    data2 = Serial.read() & 0xff;
    data3 = Serial.read() & 0xff;
    data4 = Serial.read() & 0xff;

    //Eight byte: checksum value
    unsigned char checkSum = Serial.read() & 0xff;
    
    //primitive checksum: just add all values together, and let it overflow at will
    unsigned char check = checkM + command + value + data1 + data2 + data3 + data4;

    //If checksum is not correct, something went wrong!
    if(check != checkSum)
    {
      //Let the computer know
      Serial.write('e');
      Serial.write(check);
      Serial.flush();

      //If there is anything left in the Serial buffer, we're no longer interested in it
      while (Serial.available() > 0) Serial.read(); 
      return;
    }

    //There really shouldn't be anything left
    while (Serial.available()) Serial.read(); //flush

    //With the 'i' command, you can change the global intensity of the strip
    if (command == 'i') stripint = value;
    //If the command isn't 'i', it's the next program value
    else program = command;

    //Reset the program if it changed
    if (program != prevProgram)
    {
      prevProgram = program;
      counter = 0;
    }

    

    //Great succes! Me like!
    Serial.write('a');
    Serial.write(check & 0xff);
  }
}

//           --------------           EFFECTS CODE      --------------------


// Display a single color on the whole string

void showColor( unsigned char r , unsigned char g , unsigned char b , unsigned char w) {

  cli();
  for ( int p = 0; p < PIXELS; p++ ) {
    sendPixel( r , g , b , w);
  }
  sei();
  show();

}

// Fill the dots one after the other with a color
// rewrite to lift the compare out of the loop
void colorWipe(unsigned char r , unsigned char g, unsigned char b, unsigned char w, unsigned  char wait ) {
  for (unsigned int i = 0; i < PIXELS; i += (PIXELS / 60) ) {

    cli();
    unsigned int p = 0;

    while (p++ <= i) {
      sendPixel(r, g, b, w);
    }

    while (p++ <= PIXELS) {
      sendPixel(0, 0, 0, 0);

    }

    sei();
    show();
    delay(wait);
  }
}

// I rewrite this one from scrtach to use high resolution for the color wheel to look nicer on a *much* bigger string

void rainbowStep() {

  // Hue is a number between 0 and 3*256 than defines a mix of r->g->b where
  // hue of 0 = Full red
  // hue of 128 = 1/2 red and 1/2 green
  // hue of 256 = Full Green
  // hue of 384 = 1/2 green and 1/2 blue
  // ...

  unsigned int currentPixelHue = counter;

  cli();

  for (unsigned int i = 0; i < PIXELS; i++)
  {

    if (currentPixelHue >= (3 * 256)) {              // Normalize back down incase we incremented and overflowed
      currentPixelHue -= (3 * 256);
    }



    unsigned char phase = currentPixelHue >> 8;
    unsigned char step = currentPixelHue & 0xff;

    switch (phase) {

      case 0:
        sendPixel( ~step , step ,  0 , 0);
        break;

      case 1:
        sendPixel( 0 , ~step , step, 0 );
        break;

      case 2:
        sendPixel(  step , 0 , ~step , 0);
        break;

    }

    currentPixelHue += 10;


  }

  sei();

  show();
  delay(25);

  counter += value;


}


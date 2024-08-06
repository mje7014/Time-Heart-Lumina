#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "pico/mutex.h"

#define UART_ID uart0
#define BAUD_RATE 9600
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5
#define RTC_ADDRESS 0x68 // DS3231 I2C address
#define NORMALLEDSLEEPTIME 2000
#define ANIVERSARRYLEDSLEEPTIME 750
#define SEVENSEGSLEEPTIME 100

mutex_t global_vars_mutex;

const bool binaryPatterns[10][7] = { // binary sequences for setting the 7 seg displays
    {0, 1, 1, 1, 1, 1, 1}, //0
    {0, 0, 0, 1, 1, 0, 0}, //1
    {1, 1, 0, 1, 0, 1, 1}, //2
    {1, 1, 0, 1, 1, 0, 1}, //3
    {1, 0, 1, 1, 1, 0, 0}, //4
    {1, 1, 1, 0, 1, 0, 1}, //5
    {1, 0, 1, 0, 1, 1, 1}, //6
    {0, 1, 0, 1, 1, 0, 0}, //7
    {1, 1, 1, 1, 1, 1, 1}, //8
    {1, 1, 1, 1, 1, 0, 0}  //9
};

const int segmentLEDPins[7]={2,3,6,7,8,9,10}; //all of the 7 segment display pins to control the LED states within the display
const int segmentControlPins[10]={28,27,26,22,21,20,19,18,17,16}; //7 segment display control pins. Selects which 7 seg display to enable.
const int LEDSelectPins[5]={14,15,11,12,13}; //Outer heart shaped LED ring control pins.
    //eventually i2c PWM boards will be used to control all LEDs individually

struct timeInfo{ //time info object
    int seconds;
    int minutes;
    int hours;
    int days;
    int months;
    int years;
};

struct timeInfo currenttime; //Used to store the current time as read from the RTC

struct timeInfo aniversary = { //The Users anniversary date
    .seconds = 0,
    .minutes = 20,
    .hours = 20,
    .days = 31,
    .months = 5,
    .years = 21
};

void i2c_setup() {
    i2c_init(I2C_PORT, 100 * 1000); // Initialize I2C to 100kHz
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

void query_rtc() { //set the current time by asking the RTC what time it is (blocking might not be good) should set timeout and error handling
    uint8_t buffer[7];
    i2c_write_blocking(I2C_PORT, RTC_ADDRESS, (uint8_t[]){0x00}, 1, true);
    i2c_read_blocking(I2C_PORT, RTC_ADDRESS, buffer, 7, false);
    currenttime.seconds = (buffer[0] & 0x0F) + ((buffer[0] & 0x70) >> 4) * 10; //Buffer[0] is seconds
    currenttime.minutes = (buffer[1] & 0x0F) + ((buffer[1] & 0x70) >> 4) * 10; //Buffer[1] is minutes
    currenttime.hours = (buffer[2] & 0x0F) + ((buffer[2] & 0x30) >> 4) * 10;   //Buffer[2] is hours
    currenttime.days =(buffer[4] & 0x0F)+((buffer[4] & 0x30)>>4)*10;           //Buffer[3] is day of the week which is NA buffer[4] is days
    currenttime.months= (buffer[5] & 0x0F)+((buffer[5] & 0x10)>>4)*10;         //Buffer[5] is months
    currenttime.years= (buffer[6] & 0x0F)+((buffer[6] & 0xF0)>>4)*10;          //Buffer[6] is years
}

void set7seg(int num){ //set the 7 segment display pins to represent the number
    for(int i = 0; i < 7; i++){
        gpio_put(segmentLEDPins[i], binaryPatterns[num][i]);
    }
}

void initPins(){ //initializes all the pins that are used.
    //7 segment LED Pins
    gpio_init(2);
    gpio_set_dir(2, GPIO_OUT);
    gpio_init(3);
    gpio_set_dir(3, GPIO_OUT);   
    gpio_init(6);
    gpio_set_dir(6, GPIO_OUT);  
    gpio_init(7);
    gpio_set_dir(7, GPIO_OUT);  
    gpio_init(8);
    gpio_set_dir(8, GPIO_OUT);  
    gpio_init(9);
    gpio_set_dir(9, GPIO_OUT);  
    gpio_init(10);
    gpio_set_dir(10, GPIO_OUT); 
    //select pins for the 7 segment displays
    gpio_init(28);
    gpio_set_dir(28, GPIO_OUT);
    gpio_init(27);
    gpio_set_dir(27, GPIO_OUT);
    gpio_init(26);
    gpio_set_dir(26, GPIO_OUT);
    gpio_init(22);
    gpio_set_dir(22, GPIO_OUT);
    gpio_init(21);
    gpio_set_dir(21, GPIO_OUT);
    gpio_init(20);
    gpio_set_dir(20, GPIO_OUT);
    gpio_init(19);
    gpio_set_dir(19, GPIO_OUT);
    gpio_init(18);
    gpio_set_dir(18, GPIO_OUT);
    gpio_init(17);
    gpio_set_dir(17, GPIO_OUT);
    gpio_init(16);
    gpio_set_dir(16, GPIO_OUT); 
    //Outer heart shaped LEDs (5 control groups)
    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);
    gpio_init(11);
    gpio_set_dir(11, GPIO_OUT);
    gpio_init(12);
    gpio_set_dir(12, GPIO_OUT);
    gpio_init(13);
    gpio_set_dir(13, GPIO_OUT);
    gpio_init(14);
    gpio_set_dir(14, GPIO_OUT);
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

struct timeInfo TimeDifference(){ //return a struct of the timedifference between the current time and 
    //anniverssary which is then used to set the display
    const int daysInaMonth[]={0,31,28,31,30,31,30,31,31,30,30,31};
    struct timeInfo TimeDifference;
    int currentCarry=0; //can either be 0 or -1 depending on what to carry over.
    //seconds difference
    int secondsDiff=currenttime.seconds-aniversary.seconds;
    if ((secondsDiff)<0){
        TimeDifference.seconds=60+secondsDiff;
        currentCarry=-1;
    }else{
        TimeDifference.seconds=secondsDiff;
    }
    //minutes diff
    int minutesDiff=currenttime.minutes-aniversary.minutes+currentCarry;
    if ((minutesDiff)<0){
        TimeDifference.minutes=60+minutesDiff;
        currentCarry=-1;
    }else{
        TimeDifference.minutes=minutesDiff;
        currentCarry=0;
    }
    //hours diff
    int hoursDiff=currenttime.hours-aniversary.hours+currentCarry;
    if ((hoursDiff)<0){
        TimeDifference.hours=24+hoursDiff;
        currentCarry=-1;
    }else{
        TimeDifference.hours=hoursDiff;
        currentCarry=0;
    }
    //days diff here is where it gets difficult because different months have different days in them
    //The rule is that if the year is divisible by 100 and not 400, the leap year is skipped. For example,
    // the year 2000 was a leap year, but the year 1900 was not.
    //extra day in feb
    int daysDiff=currenttime.days-aniversary.days+currentCarry;
    if ((daysDiff)<0){
        //find the difference between aniversary day and the end of that month add the current day
        int DaysInAnniversaryMonth;
        if (aniversary.months == 2 && isLeapYear(aniversary.years)){ //checks if anniverssary is a leap year make days in anniversary month global
            DaysInAnniversaryMonth=29;
        }else{
            DaysInAnniversaryMonth=daysInaMonth[aniversary.months];
        }    
        int endofMonth= DaysInAnniversaryMonth-aniversary.days;
        TimeDifference.days=endofMonth+currenttime.days;
        currentCarry=-1;
    }else{
        TimeDifference.days=daysDiff;
        currentCarry=0;
    }
    //months diff
    int monthsDiff=currenttime.months-aniversary.months+currentCarry;
    if ((monthsDiff)<0){
        TimeDifference.months=12+monthsDiff;
        currentCarry=-1;
    }else{
        TimeDifference.months=monthsDiff;
        currentCarry=0;
    }
    //years diff
    int yearsDiff=currenttime.years-aniversary.years+currentCarry;
    TimeDifference.years=yearsDiff;
    return TimeDifference;  
}

void core1_entry() {//handles all of the LED stuff
    while (true) {
        mutex_enter_blocking(&global_vars_mutex);
        if (currenttime.months==aniversary.months && currenttime.days==aniversary.days && currenttime.hours==aniversary.hours && currenttime.minutes==aniversary.minutes )//go crazy
        {
            mutex_exit(&global_vars_mutex);
            gpio_put(LEDSelectPins[0], 1);
            sleep_ms (ANIVERSARRYLEDSLEEPTIME);
            gpio_put(LEDSelectPins[1], 1);
            gpio_put(LEDSelectPins[2], 1);
            sleep_ms (ANIVERSARRYLEDSLEEPTIME);
            gpio_put(LEDSelectPins[3], 1);
            gpio_put(LEDSelectPins[4], 1);
            sleep_ms (ANIVERSARRYLEDSLEEPTIME);
            for(int i=0; i<5; i++)
            {
                gpio_put(LEDSelectPins[i], 0);
            }
            sleep_ms (ANIVERSARRYLEDSLEEPTIME);
            for (int x=0; x<3; x++){
                    for(int i=0; i<5; i++)
                {
                    gpio_put(LEDSelectPins[i], 1);
                }
                sleep_ms (ANIVERSARRYLEDSLEEPTIME);
                for(int i=0; i<5; i++)
                {
                    gpio_put(LEDSelectPins[i], 0);
                }
                sleep_ms (ANIVERSARRYLEDSLEEPTIME);
            }
        }
        else // do the normal pattern
        {
            mutex_exit(&global_vars_mutex);
            gpio_put(LEDSelectPins[0], 1);
            sleep_ms (NORMALLEDSLEEPTIME);
            gpio_put(LEDSelectPins[1], 1);
            gpio_put(LEDSelectPins[2], 1);
            sleep_ms (NORMALLEDSLEEPTIME);
            gpio_put(LEDSelectPins[3], 1);
            gpio_put(LEDSelectPins[4], 1);
            sleep_ms (NORMALLEDSLEEPTIME);
            for(int i=0; i<5; i++)
            {
                gpio_put(LEDSelectPins[i], 0);
            }
            sleep_ms (NORMALLEDSLEEPTIME);
        }
    }
}

void core0_task() { //handles all the display time stuff
    struct timeInfo timediff;
    while(true){
        mutex_enter_blocking(&global_vars_mutex); // TimeDifference andquery_rtc accesses the shared variable (current time) so lock the mutex
        query_rtc(); //maybe move this to the other thread because it doesnt have to be run so frequently as it is
        timediff = TimeDifference();
        mutex_exit(&global_vars_mutex); // Unlock the mutex after accessing the global variables
        
        int tensHours=timediff.hours/10;
        int tensMinutes=timediff.minutes/10;
        int tensDays=timediff.days/10;
        int tensMonths=timediff.months/10;
        int tensYears=timediff.years/10;
        gpio_put(segmentControlPins[0], 1);
        set7seg(timediff.years%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[0], 0);
        gpio_put(segmentControlPins[1], 1);
        set7seg(tensYears%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[1], 0);
        gpio_put(segmentControlPins[2], 1);
        set7seg(timediff.months%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[2], 0);
        gpio_put(segmentControlPins[3], 1);
        set7seg(tensMonths%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[3], 0);
        gpio_put(segmentControlPins[4], 1);
        set7seg(timediff.days%10);
        sleep_us(SEVENSEGSLEEPTIME);
        
        gpio_put(segmentControlPins[4], 0);
        gpio_put(segmentControlPins[5], 1);
        set7seg(tensDays%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[5], 0);
        gpio_put(segmentControlPins[6], 1);
        set7seg(timediff.hours%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[6], 0);
        gpio_put(segmentControlPins[7], 1);
        set7seg(tensHours%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[7], 0);
        gpio_put(segmentControlPins[8], 1);
        set7seg(timediff.minutes%10);
        sleep_us(SEVENSEGSLEEPTIME);

        gpio_put(segmentControlPins[8], 0);
        gpio_put(segmentControlPins[9], 1);
        set7seg(tensMinutes%10);
        sleep_us(SEVENSEGSLEEPTIME);
        gpio_put(segmentControlPins[9], 0);
    }   
}

int main() {
    stdio_init_all();
    initPins();
    i2c_setup();
    mutex_init(&global_vars_mutex); 
    multicore_launch_core1(core1_entry); // Launch the task on core 1 (Heart shaped LED pattern)  
    core0_task(); // Run the task on core 0 (Time display)
    return 0;
}
 
// void setRTCUart() {  //used to set the RTC with the current time. This will be tied to a button in the future
//     uart_init(UART_ID, BAUD_RATE);
//     gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
//     gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
//     uart_set_hw_flow(UART_ID, false, false);
//     uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
//     char buff[21] = {0};
//     uart_read_blocking(UART_ID, buff, 19); // Have to run time setter script or this will block. (should set time out)
//     buff[19]='\n';
//     uart_write_blocking(UART_ID, buff, 20); //writes it back to the terminal (used for verification)
//     struct timeInfo ti;
//     ti = parseDateTime(buff);
//     uint8_t timeData[7]; // Array for DS3231 time data
//     prepareTimeData(&ti, timeData);
//     uint8_t timeToSet[8]; // Additional byte for the register address
//     timeToSet[0] = 0x00; // Starting register address
//     memcpy(&timeToSet[1], timeData, sizeof(timeData));
//     // Send the time to DS3231
//     i2c_write_blocking(I2C_PORT, RTC_ADDRESS, timeToSet, sizeof(timeToSet), false);
// }

// struct timeInfo parseDateTime(const char* dateTimeStr) { //helper function for setRTCUart
//     // Use sscanf to directly parse the formatted date-time string
//     struct timeInfo tiTemp;
//     sscanf(dateTimeStr, "%d-%d-%d-%d-%d-%d",
//                            &tiTemp.years, &tiTemp.months, &tiTemp.days,
//                            &tiTemp.hours, &tiTemp.minutes, &tiTemp.seconds);
//     return tiTemp;
// }

// void prepareTimeData(const struct timeInfo* ti, uint8_t* timeToSet) {
//     timeToSet[0] = intToBcd(ti->seconds);
//     timeToSet[1] = intToBcd(ti->minutes);
//     timeToSet[2] = intToBcd(ti->hours);
//     timeToSet[3] = 0; // Day of week not set, so set to 0 or some default value
//     timeToSet[4] = intToBcd(ti->days);
//     timeToSet[5] = intToBcd(ti->months);
//     timeToSet[6] = intToBcd(ti->years % 100); // Only last two digits of the year for DS3231
// }

// uint8_t intToBcd(int value) {
//     return ((value / 10) << 4) | (value % 10);
// }
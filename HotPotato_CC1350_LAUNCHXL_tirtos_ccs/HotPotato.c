/*
 * Copyright (c) 2015-2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== rfEasyLinkTx.c ========
 */

/***** Includes *****/
#include <Board.h>
#include <easylink/EasyLink.h>
#include <stdint.h>
#include <stdlib.h>
#include <ti/devices/cc13x0/driverlib/cpu.h>
#include <ti/drivers/PIN.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/System.h>
#include <xdc/std.h>

/***** Defines *****/
#define RFEASYLINKEX_TASK_STACK_SIZE 1024
#define RFEASYLINKEX_TASK_PRIORITY   2
#define RFEASYLINKTX_TASK_STACK_SIZE    1024
#define RFEASYLINKTX_TASK_PRIORITY      3
#define RFEASYLINKTXPAYLOAD_LENGTH      6
#define TOTAL_TIME 10.0
#define LEFT_BUTTON 0
#define RIGHT_BUTTON 1
#define YOUR_TURN_MESSAGE_TYPE 1
#define HI_MY_INDEX_IS_MESSAGE_TYPE 2
#define IM_DEAD_MESSAGE_TYPE 3
#define SECOND_ROUND_MESSAGE_TYPE 4

/*States description:
    0- not inited
    1- I'm first and waiting for initialization round to finish
    2- I'm not first and didn't double press yet
    3- I'm not first and waiting for initialization round to finish
    4- Light at someone else
    5- Light at me
    6- Dead
    7- Won
*/
#define NOTHING_DONE 0
#define IM_FIRST_WAITING_FOR_CIRCLE 1
#define IM_NOT_FIRST_NOT_IN_CIRCLE 2
#define IM_NOT_FIRST_WAITING_FOR_CIRCLE 3
#define WAITING_FOR_MY_TURN 4
#define MY_TURN 5
#define IM_DEAD 6
#define IVE_WON 7

/***** Logic Variables *****/
int left_friend = 0;
int right_friend = 2;
int left_friend_physically = 0;
int right_friend_physically = 2;
int my_index = 1;
double remaining_time_hundreth_sec = 1000.0;//10 seconds
int state = 0;

/***** Button and Led Variables and Inits *****/
PIN_Config pin_table[] = {
    Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_LED2 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};
PIN_Config button_pin_table[] = {
    Board_PIN_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    Board_PIN_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};
static PIN_Handle led_pin_handle;
static PIN_State led_pin_state;
PIN_Handle pin_handle;
static PIN_Handle button_pin_handle;
static PIN_State button_pin_state;

/***** Clock and Time-keeping Variables *****/
Clock_Handle general_clock_handle;
Clock_Handle clockHandle;
int first_click_of_double = 1;// used to detect double clicks vs normal click
double timer_delta_hundreth_sec = 30.0;
int to_deduct = 0;

/***** Communication related Variables *****/
static uint8_t seq_number =(uint8_t)0;
uint8_t send_message_type;
uint8_t send_message_content1;
uint8_t send_message_content2;
uint8_t send_message_content3;
uint8_t send_message_content4;
uint8_t recv_message_type;
uint8_t recv_message_content1;
uint8_t recv_message_content2;
uint8_t recv_message_content3;
uint8_t recv_message_content4;
Semaphore_Struct sem_struct;
Semaphore_Handle sem_handle;
Semaphore_Struct sem_struct2;
Semaphore_Handle sem_handle2;
static Task_Params rx_task_params;
Task_Struct rx_task;
static uint8_t rx_taskStack[RFEASYLINKEX_TASK_STACK_SIZE];
Task_Struct tx_task;
static Task_Params tx_task_params;
static uint8_t tx_task_stack[RFEASYLINKTX_TASK_STACK_SIZE];
int cnf=0;



void turn_green_on()
{
    PIN_setOutputValue(led_pin_handle, Board_LED1, 1);
}
void turn_green_off()
{
    PIN_setOutputValue(led_pin_handle, Board_LED1, 0);
}
void turn_red_on()
{
    PIN_setOutputValue(led_pin_handle, Board_LED2, 1);
}
void turn_red_off()
{
    PIN_setOutputValue(led_pin_handle, Board_LED2, 0);
}
void turn_red_if_in_danger(){
    if(remaining_time_hundreth_sec<200){//turn on red on last 2 seconds
        turn_red_on();
    }
}
/**
 * Blink both leds in the amount of times
 */
void double_led_blink(int times)
{
    if(times==1){
        PIN_setOutputValue(led_pin_handle, Board_LED2, !PIN_getOutputValue(Board_LED2));
        PIN_setOutputValue(led_pin_handle, Board_LED1, !PIN_getOutputValue(Board_LED1));
        return;
    }
    int i;
    for (i = 0; i < times; i++)
    {
        turn_red_on();
        turn_green_on();
        CPUdelay(8000 * 20);
        turn_red_off();
        turn_green_off();
        CPUdelay(8000 * 20);
    }
}
void ive_won_animation()//Celebrate
{
    int i;
    for (i = 0; i < 10; i++)
    {
        turn_red_on();
        turn_green_on();
        CPUdelay(8000 * 100);
        turn_red_off();
        turn_green_off();
        CPUdelay(8000 * 100);
    }
}

void ive_died_animation()
{
    //turn on both lights
    turn_red_on();
    turn_green_on();
}
void resume_time_deduction(){
    to_deduct = 1;
}
void stop_time_deduction(){
    to_deduct = 0;
}
int randomSelect(int ind1, int ind2)
{
    int i = rand();
    if (i % 2 == 0)
    {
        return ind1;
    }
    return ind2;
}

void send_message_logic(int msg_type,int msg_cont1,int msg_cont2,int msg_cont3,int msg_cont4){
    EasyLink_abort();
    /*set the logical message fields*/
    send_message_type=msg_type;
    send_message_content1 = msg_cont1;
    send_message_content2 = msg_cont2;
    send_message_content3 = msg_cont3;
    send_message_content4 = msg_cont4;

    Semaphore_post(sem_handle);
}

/**
* Send a message of type "Im dead_index, and I've died, and here are my neihbors and next guys turn".
*/
void send_im_dead(int dead_index, int deads_left, int deads_right, int next_guys_turn)
{
   seq_number+=1;
   send_message_logic(IM_DEAD_MESSAGE_TYPE,dead_index,deads_left,deads_right,next_guys_turn);
}

void kill_clock(){
    Clock_delete(&general_clock_handle);
}
int blinkNum=0;
void general_timer_tick(UArg arg)
{
    if(to_deduct!=0){
        turn_red_if_in_danger();
        remaining_time_hundreth_sec= remaining_time_hundreth_sec-timer_delta_hundreth_sec;
        if (remaining_time_hundreth_sec <= 0)//out of time
        {
            state = IM_DEAD;
            stop_time_deduction();
            kill_clock();
            ive_died_animation();
            //notify I'm dead
            send_im_dead(my_index, left_friend, right_friend,
                       randomSelect(left_friend, right_friend));
        }
    }
}
void start_general_timer(){
    Clock_Params clock_params;
    Clock_Params_init(&clock_params);
    clock_params.period = timer_delta_hundreth_sec*1000;
    clock_params.startFlag = FALSE;
    general_clock_handle = Clock_create(general_timer_tick,timer_delta_hundreth_sec*1000, &clock_params, NULL);
    Clock_start(general_clock_handle);
}



static void rf_easylink_tx_fnx(UArg arg0, UArg arg1)
{
    if(cnf==0){cnf=1;EasyLink_init(EasyLink_Phy_5kbpsSlLr);Semaphore_post(sem_handle);}
    else{
        Semaphore_pend(sem_handle, BIOS_WAIT_FOREVER);
    }

    while(1) {
        EasyLink_TxPacket tx_packet =  { {0}, 0, 0, {0} };
        Semaphore_pend(sem_handle, 1000);
        /* Create packet with incrementing sequence number and random payload */
        tx_packet.payload[0] = (uint8_t)(seq_number);
        tx_packet.payload[1] = send_message_type;
        tx_packet.payload[2] = send_message_content1;
        tx_packet.payload[3] = send_message_content2;
        tx_packet.payload[4] = send_message_content3;
        tx_packet.payload[5] = send_message_content4;

        tx_packet.len = RFEASYLINKTXPAYLOAD_LENGTH;
        tx_packet.dstAddr[0] = 0xaa;
        /* Set Tx absolute time to current time + 100ms */
        tx_packet.absTime = EasyLink_getAbsTime() + EasyLink_ms_To_RadioTime(100);
        int times;
        for(times=0;times<2;times++){
            EasyLink_abort();
            EasyLink_Status result = EasyLink_transmit(&tx_packet);
        }
        Semaphore_post(sem_handle2);
    }
}

void tx_task_init(PIN_Handle in_pin_handle) {
    led_pin_handle = in_pin_handle;

    Task_Params_init(&tx_task_params);
    tx_task_params.stackSize = RFEASYLINKTX_TASK_STACK_SIZE;
    tx_task_params.priority = RFEASYLINKTX_TASK_PRIORITY;
    tx_task_params.stack = &tx_task_stack;
    tx_task_params.arg0 = (UInt)1000000;

    Task_construct(&tx_task, rf_easylink_tx_fnx, &tx_task_params, NULL);
}



/**
 * When we got a "your turn" message
 */
void received_your_turn(int i)
{
    if(i==my_index){
        state = MY_TURN;
        turn_green_on();
        turn_red_off();
        turn_red_if_in_danger();
        resume_time_deduction();
    }
    else
    {
        state = WAITING_FOR_MY_TURN;
    turn_red_off();
    turn_green_off();
    }
}
/**
 * When we got a "I'm dead" message
 */
void received_im_dead(int dead_index, int deads_left, int deads_right,
                    int next_guys_turn)
{
    //fix neighbors
    if (left_friend == dead_index)
    {
        left_friend = deads_left;
    }
    if (right_friend == dead_index)
    {
        right_friend = deads_right;
    }
    if (right_friend == my_index) //I'm a neighbor of myself, i.e., I won
    {
        kill_clock();
        ive_won_animation();
        stop_time_deduction();// for case the other guy died because he pressed when it wasn't his turn, and it's my turn now
        state=IVE_WON;
    }else if(next_guys_turn == my_index){//if it's my move, and it's not over, act like it
        received_your_turn(my_index);
    }
}

void init_general_game_data()
{
    remaining_time_hundreth_sec = 1000.0;//10 seconds
    to_deduct = 0;
    start_general_timer();
}
/**
 * When we got a "hi my index is" message
 */
void received_hi_my_index_is(int i)
{
    switch (state)
    {
    case NOTHING_DONE: //First guy is talking and it isn't me, before I've talked at all
        state = IM_NOT_FIRST_NOT_IN_CIRCLE;
        left_friend = i;
        left_friend_physically = i;
        my_index = i+1;
        right_friend = 1;
        right_friend_physically = 1;
        break; 
    case IM_FIRST_WAITING_FOR_CIRCLE: //I'm first, and the circle expanded by 1
        state = IM_FIRST_WAITING_FOR_CIRCLE;
        left_friend = i;
        left_friend_physically = i;
        break;
    case IM_NOT_FIRST_NOT_IN_CIRCLE: //Some other guy is talking and it isn't me, before I've talked at all
        state = IM_NOT_FIRST_NOT_IN_CIRCLE;
        left_friend = i;
        left_friend_physically = i;
        my_index = i+1;
        break;
    case IM_NOT_FIRST_WAITING_FOR_CIRCLE: //Some other guy is talking after I did, therefore he's my new right neighbor
        state = WAITING_FOR_MY_TURN;
        right_friend = i;
        right_friend_physically = i;
        break;
    }
}

void handle_message(){
    switch (recv_message_type)
    {
    case YOUR_TURN_MESSAGE_TYPE:
        if(state==IM_DEAD){// If I'm dead, it means it's a new round, and it's my turn. I need to refresh...
            init_general_game_data();
            right_friend = right_friend_physically;
            left_friend = left_friend_physically;
            turn_red_off();
            turn_green_off();
        }
        received_your_turn(recv_message_content1);
        break;
    case HI_MY_INDEX_IS_MESSAGE_TYPE:
        double_led_blink(1);
        received_hi_my_index_is(recv_message_content1);
        break;
    case IM_DEAD_MESSAGE_TYPE:
        received_im_dead(recv_message_content1, recv_message_content2,recv_message_content3, recv_message_content4);
        break;
    }
}
void received_message_logic(EasyLink_RxPacket rx_packet){
    //resend the message
    seq_number=rx_packet.payload[0];
    send_message_logic(rx_packet.payload[1],rx_packet.payload[2],rx_packet.payload[3],rx_packet.payload[4],rx_packet.payload[5]);

    recv_message_type = rx_packet.payload[1];
    recv_message_content1 = rx_packet.payload[2];
    recv_message_content2 = rx_packet.payload[3];
    recv_message_content3 = rx_packet.payload[4];
    recv_message_content4 = rx_packet.payload[5];

    handle_message();
}


static void rf_easylink_rx_fnx(UArg arg0, UArg arg1)
{
    if(cnf==0){cnf=1;EasyLink_init(EasyLink_Phy_5kbpsSlLr);Semaphore_post(sem_handle);}
    else{
        Semaphore_pend(sem_handle, BIOS_WAIT_FOREVER);
    }
    EasyLink_RxPacket rx_packet = {0};
    while (1)
    {
        rx_packet.absTime = 0;
        Semaphore_pend(sem_handle2, BIOS_WAIT_FOREVER);
        EasyLink_Status result;
        result = EasyLink_receive(&rx_packet);

        if (result == EasyLink_Status_Success)
        {
            if ((int)rx_packet.payload[0] > seq_number /*Didn't Process This Message Yet. Otherwise do nothing*/)
            {
                received_message_logic(rx_packet);
            }
            Semaphore_post(sem_handle);
        }
    }
}

void rx_task_init(PIN_Handle led_pin_handle) {
    pin_handle = led_pin_handle;

    Task_Params_init(&rx_task_params);
    rx_task_params.stackSize = RFEASYLINKEX_TASK_STACK_SIZE;
    rx_task_params.priority = RFEASYLINKEX_TASK_PRIORITY;
    rx_task_params.stack = &rx_taskStack;
    rx_task_params.arg0 = (UInt)1000000;

    Task_construct(&rx_task, rf_easylink_rx_fnx, &rx_task_params, NULL);
}


/**
 * generate random number between 1,...,n
 */
int generate_random_start(int n)
{
    return (rand() % n) + 1;
}

/**
 * Send a message of type "its your turn mister i"
 */
void send_your_turn(int i)
{
    seq_number+=1;
    send_message_logic(YOUR_TURN_MESSAGE_TYPE,i,0/*Null*/,0/*Null*/,0/*Null*/);
}
/**
 * Send a message of type "hi i'm new and my index is i"
 */
void send_hi_my_index_is(int i)
{
    seq_number+=1;
    send_message_logic(HI_MY_INDEX_IS_MESSAGE_TYPE,i,0/*Null*/,0/*Null*/,0/*Null*/);
}


/**
 * What to do when we register a double button press
 */
void double_button_reaction()
{
    int startingIndex;
    switch (state)
    {
    case NOTHING_DONE: //I'm the first to talk
        right_friend = 2;
        right_friend_physically = 2;
        send_hi_my_index_is(my_index);
        state = IM_FIRST_WAITING_FOR_CIRCLE;
        double_led_blink(1);
        break;
    case IM_NOT_FIRST_NOT_IN_CIRCLE: //I'm not the first to talk
        send_hi_my_index_is(my_index);
        state = IM_NOT_FIRST_WAITING_FOR_CIRCLE;
        double_led_blink(2);
        break;
    case IM_FIRST_WAITING_FOR_CIRCLE: //I'm closing the circle
        double_led_blink(3);
        state = WAITING_FOR_MY_TURN;
        startingIndex = generate_random_start(left_friend);
        if(startingIndex==my_index){
            received_your_turn(my_index);
        } else {
            send_your_turn(startingIndex);
        }
        break;
    case IVE_WON: //Starting new game (can only be done by winner)
        init_general_game_data();
        right_friend = right_friend_physically;
        left_friend = left_friend_physically;
        double_led_blink(10);
        received_your_turn(my_index);
        break;
    }
}
void single_button_reaction(int button_pressed)
{
    switch (state)
    {
    case WAITING_FOR_MY_TURN:// I pressed when it wasn't my turn
        state = IM_DEAD;
        ive_died_animation();
        //notify I'm dead
        send_im_dead(my_index, left_friend, right_friend,100/*No one is gets the next turn*/);
        break;
    case MY_TURN: //I pressed button before my time finished
        //turn of my lights
        turn_red_off();
        turn_green_off();
        state = WAITING_FOR_MY_TURN;
        stop_time_deduction();
        if(button_pressed == RIGHT_BUTTON){
            send_your_turn(right_friend);
        } else{
            send_your_turn(left_friend);
        }
        break;
    }

}
void button_callback_fxn(PIN_Handle handle, PIN_Id pinId) {
    /* Debounce logic, only toggle if the button is still pushed (low) */
    CPUdelay(8000*100);
    if (!PIN_getInputValue(Board_PIN_BUTTON0) && !PIN_getInputValue(Board_PIN_BUTTON1)){
        first_click_of_double=1-first_click_of_double;
        if(!first_click_of_double){//########## double click action
            double_button_reaction();

        } else {
            return;// if it's the second alert of the double click, just don't do anything.
        }
    }
    else if (!PIN_getInputValue(pinId)) {
        switch (pinId) {
            case Board_PIN_BUTTON1: //################## right click action
                single_button_reaction(RIGHT_BUTTON);
                break;

            case Board_PIN_BUTTON0: //################## left click action
                single_button_reaction(LEFT_BUTTON);
                break;
        }
    }
}
/*
 *  ======== main ========
 */
int main(void)
{

    /* create RX/TX semaphore */
     Semaphore_Params sem_params;
     sem_params.mode = ti_sysbios_knl_Semaphore_Mode_BINARY;
     Semaphore_Params sem_params2;
     sem_params2.mode = ti_sysbios_knl_Semaphore_Mode_BINARY;

    /* Call board init functions. */
    Board_initGeneral();

    /* Setup callback for LED pins */
    led_pin_handle = PIN_open(&led_pin_state, pin_table);
    if(!led_pin_handle) {
        System_abort("Error initializing board LED pins\n");
    }
    /* Setup callback for button pins */
    button_pin_handle = PIN_open(&button_pin_state, button_pin_table);
    if(!button_pin_handle) {while(1);}
    if (PIN_registerIntCb(button_pin_handle, &button_callback_fxn) != 0) {while(1);}

    /* Clear LED pins */
    PIN_setOutputValue(led_pin_handle, Board_LED1, 0);
    PIN_setOutputValue(led_pin_handle, Board_LED2, 0);

    /* Init semaphore 1 */
    Semaphore_Params_init(&sem_params);
    Semaphore_construct(&sem_struct, 1, &sem_params);
    sem_handle = Semaphore_handle(&sem_struct);
    /* Init semaphore 2 */
    Semaphore_Params_init(&sem_params2);
    Semaphore_construct(&sem_struct2, 1, &sem_params2);
    sem_handle2 = Semaphore_handle(&sem_struct2);

    rx_task_init(led_pin_handle);
    tx_task_init(led_pin_handle);

    init_general_game_data();
    /* Start BIOS */
    BIOS_start();

    return (0);
}

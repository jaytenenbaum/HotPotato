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

/***** Logic Variables *****/
int leftFriend = 0;
int rightFriend = 2;
int leftFriendPhysically = 0;
int rightFriendPhysically = 2;
int myIndex = 1;
double remainingTimeHundrethSeconds = 1000.0;//10 seconds
int state = 0;
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


/***** Button and Led Variables and Inits *****/
PIN_Config pinTable[] = {
    Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_LED2 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};
PIN_Config buttonPinTable[] = {
    Board_PIN_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    Board_PIN_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};
static PIN_Handle ledPinHandle;
static PIN_State ledPinState;
PIN_Handle pinHandle;
static PIN_Handle buttonPinHandle;
static PIN_State buttonPinState;

/***** Clock and Time-keeping Variables *****/
Clock_Handle generalClockHandle;
Clock_Handle clockHandle;
int firstClickOfDouble = 1;// used to detect double clicks vs normal click
double timerDeltaHundrethSec = 30.0;
int toDeduct = 0;

/***** Communication related Variables *****/
static uint8_t seqNumber =(uint8_t)0;
uint8_t sendMessageType;
uint8_t sendMessageContent1;
uint8_t sendMessageContent2;
uint8_t sendMessageContent3;
uint8_t sendMessageContent4;
uint8_t recvMessageType;
uint8_t recvMessageContent1;
uint8_t recvMessageContent2;
uint8_t recvMessageContent3;
uint8_t recvMessageContent4;
Semaphore_Struct semStruct;
Semaphore_Handle semHandle;
Semaphore_Struct semStruct2;
Semaphore_Handle semHandle2;
static Task_Params rxTaskParams;
Task_Struct rxTask;
static uint8_t rxTaskStack[RFEASYLINKEX_TASK_STACK_SIZE];
Task_Struct txTask;
static Task_Params txTaskParams;
static uint8_t txTaskStack[RFEASYLINKTX_TASK_STACK_SIZE];
int cnf=0;



void turnGreenOn()
{
    PIN_setOutputValue(ledPinHandle, Board_LED1, 1);
}
void turnGreenOff()
{
    PIN_setOutputValue(ledPinHandle, Board_LED1, 0);
}
void turnRedOn()
{
    PIN_setOutputValue(ledPinHandle, Board_LED2, 1);
}
void turnRedOff()
{
    PIN_setOutputValue(ledPinHandle, Board_LED2, 0);
}
void turnRedIfInDanger(){
    if(remainingTimeHundrethSeconds<200){//turn on red on last 2 seconds
        turnRedOn();
    }
}
/**
 * Blink both leds in the amount of times
 */
void doubleLedBlink(int times)
{
    if(times==1){
        PIN_setOutputValue(ledPinHandle, Board_LED2, !PIN_getOutputValue(Board_LED2));
        PIN_setOutputValue(ledPinHandle, Board_LED1, !PIN_getOutputValue(Board_LED1));
        return;
    }
    int i;
    for (i = 0; i < times; i++)
    {
        turnRedOn();
        turnGreenOn();
        CPUdelay(8000 * 20);
        turnRedOff();
        turnGreenOff();
        CPUdelay(8000 * 20);
    }
}
void IveWonAnimation()//Celebrate
{
    int i;
    for (i = 0; i < 10; i++)
    {
        turnRedOn();
        turnGreenOn();
        CPUdelay(8000 * 100);
        turnRedOff();
        turnGreenOff();
        CPUdelay(8000 * 100);
    }
}

void IveDiedAnimation()
{
    //turn on both lights
    turnRedOn();
    turnGreenOn();
}
void resumeTimeDeduction(){
    toDeduct = 1;
}
void stopTimeDeduction(){
    toDeduct = 0;
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

void sendMessageLogic(int msgType,int msgCont1,int msgCont2,int msgCont3,int msgCont4){
    EasyLink_abort();
    /*set the logical message fields*/
    sendMessageType=msgType;
    sendMessageContent1 = msgCont1;
    sendMessageContent2 = msgCont2;
    sendMessageContent3 = msgCont3;
    sendMessageContent4 = msgCont4;

    Semaphore_post(semHandle);
}

/**
* Send a message of type "Im deadIndex, and I've died, and here are my neihbors and next guys turn".
*/
void sendImDead(int deadIndex, int deadsLeft, int deadsRight, int nextGuysTurn)
{
   seqNumber+=1;
   sendMessageLogic(IM_DEAD_MESSAGE_TYPE,deadIndex,deadsLeft,deadsRight,nextGuysTurn);
}

void killClock(){
    Clock_delete(&generalClockHandle);
}
int blinkNum=0;
void generalTimerTick(UArg arg)
{
    if(toDeduct!=0){
        turnRedIfInDanger();
        remainingTimeHundrethSeconds= remainingTimeHundrethSeconds-timerDeltaHundrethSec;
        if (remainingTimeHundrethSeconds <= 0)//out of time
        {
            state = 6;
            stopTimeDeduction();
            killClock();
            IveDiedAnimation();
            //notify I'm dead
            sendImDead(myIndex, leftFriend, rightFriend,
                       randomSelect(leftFriend, rightFriend));
        }
    }
}
void startGeneralTimer(){
    Clock_Params clockParams;
    Clock_Params_init(&clockParams);
    clockParams.period = timerDeltaHundrethSec*1000;
    clockParams.startFlag = FALSE;
    generalClockHandle = Clock_create(generalTimerTick,timerDeltaHundrethSec*1000, &clockParams, NULL);
    Clock_start(generalClockHandle);
}



static void rfEasyLinkTxFnx(UArg arg0, UArg arg1)
{
    if(cnf==0){cnf=1;EasyLink_init(EasyLink_Phy_5kbpsSlLr);Semaphore_post(semHandle);}
    else{
        Semaphore_pend(semHandle, BIOS_WAIT_FOREVER);
    }

    while(1) {
        EasyLink_TxPacket txPacket =  { {0}, 0, 0, {0} };
        Semaphore_pend(semHandle, 1000);
        /* Create packet with incrementing sequence number and random payload */
        txPacket.payload[0] = (uint8_t)(seqNumber);
        txPacket.payload[1] = sendMessageType;
        txPacket.payload[2] = sendMessageContent1;
        txPacket.payload[3] = sendMessageContent2;
        txPacket.payload[4] = sendMessageContent3;
        txPacket.payload[5] = sendMessageContent4;

        txPacket.len = RFEASYLINKTXPAYLOAD_LENGTH;
        txPacket.dstAddr[0] = 0xaa;
        /* Set Tx absolute time to current time + 100ms */
        txPacket.absTime = EasyLink_getAbsTime() + EasyLink_ms_To_RadioTime(100);
        int times;
        for(times=0;times<2;times++){
            EasyLink_abort();
            EasyLink_Status result = EasyLink_transmit(&txPacket);
        }
        Semaphore_post(semHandle2);
    }
}

void txTask_init(PIN_Handle inPinHandle) {
    ledPinHandle = inPinHandle;

    Task_Params_init(&txTaskParams);
    txTaskParams.stackSize = RFEASYLINKTX_TASK_STACK_SIZE;
    txTaskParams.priority = RFEASYLINKTX_TASK_PRIORITY;
    txTaskParams.stack = &txTaskStack;
    txTaskParams.arg0 = (UInt)1000000;

    Task_construct(&txTask, rfEasyLinkTxFnx, &txTaskParams, NULL);
}



/**
 * When we got a "your turn" message
 */
void receivedYourTurn(int i)
{
    if(i==myIndex){
        state = 5;
        turnGreenOn();
        turnRedOff();
        turnRedIfInDanger();
        resumeTimeDeduction();
    }
    else
    {
        state = 4;
    turnRedOff();
    turnGreenOff();
    }
}
/**
 * When we got a "I'm dead" message
 */
void receivedImDead(int deadIndex, int deadsLeft, int deadsRight,
                    int nextGuysTurn)
{
    //fix neighbors
    if (leftFriend == deadIndex)
    {
        leftFriend = deadsLeft;
    }
    if (rightFriend == deadIndex)
    {
        rightFriend = deadsRight;
    }
    if (rightFriend == myIndex) //I'm a neighbor of myself, i.e., I won
    {
        killClock();
        IveWonAnimation();
        stopTimeDeduction();// for case the other guy died because he pressed when it wasn't his turn, and it's my turn now
        state=7;
    }else if(nextGuysTurn == myIndex){//if it's my move, and it's not over, act like it
        receivedYourTurn(myIndex);
    }
}

void initGeneralGameData()
{
    remainingTimeHundrethSeconds = 1000.0;//10 seconds
    toDeduct = 0;
    startGeneralTimer();
}
/**
 * When we got a "hi my index is" message
 */
void receivedHiMyIndexIs(int i)
{
    switch (state)
    {
    case 0: //First guy is talking and it isn't me, before I've talked at all
        state = 2;
        leftFriend = i;
        leftFriendPhysically = i;
        myIndex = i+1;
        rightFriend = 1;
        rightFriendPhysically = 1;
        break;
    case 1: //I'm first, and the circle expanded by 1
        state = 1;
        leftFriend = i;
        leftFriendPhysically = i;
        break;
    case 2: //Some other guy is talking and it isn't me, before I've talked at all
        state = 2;
        leftFriend = i;
        leftFriendPhysically = i;
        myIndex = i+1;
        break;
    case 3: //Some other guy is talking after I did, therefore he's my new right neighbor
        state = 4;
        rightFriend = i;
        rightFriendPhysically = i;
        break;
    }
}

void handleMessage(){
    switch (recvMessageType)
    {
    case YOUR_TURN_MESSAGE_TYPE:
        if(state==6){// If I'm dead, it means it's a new round, and it's my turn. I need to refresh...
            initGeneralGameData();
            rightFriend = rightFriendPhysically;
            leftFriend = leftFriendPhysically;
            turnRedOff();
            turnGreenOff();
        }
        receivedYourTurn(recvMessageContent1);
        break;
    case HI_MY_INDEX_IS_MESSAGE_TYPE:
        doubleLedBlink(1);
        receivedHiMyIndexIs(recvMessageContent1);
        break;
    case IM_DEAD_MESSAGE_TYPE:
        receivedImDead(recvMessageContent1, recvMessageContent2,recvMessageContent3, recvMessageContent4);
        break;
    }
}
void receivedMessageLogic(EasyLink_RxPacket rxPacket){
    //resend the message
    seqNumber=rxPacket.payload[0];
    sendMessageLogic(rxPacket.payload[1],rxPacket.payload[2],rxPacket.payload[3],rxPacket.payload[4],rxPacket.payload[5]);

    recvMessageType = rxPacket.payload[1];
    recvMessageContent1 = rxPacket.payload[2];
    recvMessageContent2 = rxPacket.payload[3];
    recvMessageContent3 = rxPacket.payload[4];
    recvMessageContent4 = rxPacket.payload[5];

    handleMessage();
}


static void rfEasyLinkRxFnx(UArg arg0, UArg arg1)
{
    if(cnf==0){cnf=1;EasyLink_init(EasyLink_Phy_5kbpsSlLr);Semaphore_post(semHandle);}
    else{
        Semaphore_pend(semHandle, BIOS_WAIT_FOREVER);
    }
    EasyLink_RxPacket rxPacket = {0};
    while (1)
    {
        rxPacket.absTime = 0;
        Semaphore_pend(semHandle2, BIOS_WAIT_FOREVER);
        EasyLink_Status result;
        result = EasyLink_receive(&rxPacket);

        if (result == EasyLink_Status_Success)
        {
            if ((int)rxPacket.payload[0] > seqNumber /*Didn't Process This Message Yet. Otherwise do nothing*/)
            {
                receivedMessageLogic(rxPacket);
            }
            Semaphore_post(semHandle);
        }
    }
}

void rxTask_init(PIN_Handle ledPinHandle) {
    pinHandle = ledPinHandle;

    Task_Params_init(&rxTaskParams);
    rxTaskParams.stackSize = RFEASYLINKEX_TASK_STACK_SIZE;
    rxTaskParams.priority = RFEASYLINKEX_TASK_PRIORITY;
    rxTaskParams.stack = &rxTaskStack;
    rxTaskParams.arg0 = (UInt)1000000;

    Task_construct(&rxTask, rfEasyLinkRxFnx, &rxTaskParams, NULL);
}


/**
 * generate random number between 1,...,n
 */
int generateRandomStart(int n)
{
    return (rand() % n) + 1;
}

/**
 * Send a message of type "its your turn mister i"
 */
void sendYourTurn(int i)
{
    seqNumber+=1;
    sendMessageLogic(YOUR_TURN_MESSAGE_TYPE,i,0/*Null*/,0/*Null*/,0/*Null*/);
}
/**
 * Send a message of type "hi i'm new and my index is i"
 */
void sendHiMyIndexIs(int i)
{
    seqNumber+=1;
    sendMessageLogic(HI_MY_INDEX_IS_MESSAGE_TYPE,i,0/*Null*/,0/*Null*/,0/*Null*/);
}


/**
 * What to do when we register a double button press
 */
void doubleButtonReaction()
{
    int startingIndex;
    switch (state)
    {
    case 0: //I'm the first to talk
        rightFriend = 2;
        rightFriendPhysically = 2;
        sendHiMyIndexIs(myIndex);
        state = 1;
        doubleLedBlink(1);
        break;
    case 2: //I'm not the first to talk
        sendHiMyIndexIs(myIndex);
        state = 3;
        doubleLedBlink(2);
        break;
    case 1: //I'm closing the circle
        doubleLedBlink(3);
        state = 4;
        startingIndex = generateRandomStart(leftFriend);
        if(startingIndex==myIndex){
            receivedYourTurn(myIndex);
        } else {
            sendYourTurn(startingIndex);
        }
        break;
    case 7: //Starting new game (can only be done by winner)
        initGeneralGameData();
        rightFriend = rightFriendPhysically;
        leftFriend = leftFriendPhysically;
        doubleLedBlink(10);
        receivedYourTurn(myIndex);
        break;
    }
}
void singleButtonReaction(int buttonPressed)
{
    switch (state)
    {
    case 4:// I pressed when it wasn't my turn
        state = 6;
        IveDiedAnimation();
        //notify I'm dead
        sendImDead(myIndex, leftFriend, rightFriend,100/*No one is gets the next turn*/);
        break;
    case 5: //I pressed button before my time finished
        //turn of my lights
        turnRedOff();
        turnGreenOff();
        state = 4;
        stopTimeDeduction();
        if(buttonPressed == RIGHT_BUTTON){
            sendYourTurn(rightFriend);
        } else{
            sendYourTurn(leftFriend);
        }
        break;
    }

}
void buttonCallbackFxn(PIN_Handle handle, PIN_Id pinId) {
    /* Debounce logic, only toggle if the button is still pushed (low) */
    CPUdelay(8000*100);
    if (!PIN_getInputValue(Board_PIN_BUTTON0) && !PIN_getInputValue(Board_PIN_BUTTON1)){
        firstClickOfDouble=1-firstClickOfDouble;
        if(!firstClickOfDouble){//########## double click action
            doubleButtonReaction();

        } else {
            return;// if it's the second alert of the double click, just don't do anything.
        }
    }
    else if (!PIN_getInputValue(pinId)) {
        switch (pinId) {
            case Board_PIN_BUTTON1: //################## right click action
                singleButtonReaction(RIGHT_BUTTON);
                break;

            case Board_PIN_BUTTON0: //################## left click action
                singleButtonReaction(LEFT_BUTTON);
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
     Semaphore_Params semParams;
     semParams.mode = ti_sysbios_knl_Semaphore_Mode_BINARY;
     Semaphore_Params semParams2;
     semParams2.mode = ti_sysbios_knl_Semaphore_Mode_BINARY;

    /* Call board init functions. */
    Board_initGeneral();

    /* Setup callback for LED pins */
    ledPinHandle = PIN_open(&ledPinState, pinTable);
    if(!ledPinHandle) {
        System_abort("Error initializing board LED pins\n");
    }
    /* Setup callback for button pins */
    buttonPinHandle = PIN_open(&buttonPinState, buttonPinTable);
    if(!buttonPinHandle) {while(1);}
    if (PIN_registerIntCb(buttonPinHandle, &buttonCallbackFxn) != 0) {while(1);}

    /* Clear LED pins */
    PIN_setOutputValue(ledPinHandle, Board_LED1, 0);
    PIN_setOutputValue(ledPinHandle, Board_LED2, 0);

    /* Init semaphore 1 */
    Semaphore_Params_init(&semParams);
    Semaphore_construct(&semStruct, 1, &semParams);
    semHandle = Semaphore_handle(&semStruct);
    /* Init semaphore 2 */
    Semaphore_Params_init(&semParams2);
    Semaphore_construct(&semStruct2, 1, &semParams2);
    semHandle2 = Semaphore_handle(&semStruct2);

    rxTask_init(ledPinHandle);
    txTask_init(ledPinHandle);

    initGeneralGameData();
    /* Start BIOS */
    BIOS_start();

    return (0);
}

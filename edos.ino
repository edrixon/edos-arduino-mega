
// Managed memory pool configuration
#define MEMPOOL_SIZE     64     // Number of memory blocks in pool
#define MEMBLOCK_SIZE    64      // Size of each memory block

#define OS_TICK          100     // number of ms for timers

#define MSGLEN           16      // Length of a message

#define SERBUFFLEN       80

#define LED_INIT         0
#define LED_ON           1
#define LED_OFF          2

typedef enum { LOGHIGH, LOGLOW } logicLevel;

typedef enum { PROC_RUNNING, PROC_SUSPENDED } processStateType;

typedef enum 
{
    E_OK,
    E_OUTOFMEM,
    E_NOSUCHBLOCK,
    E_NOSUCHTIMER,
    E_NOSUCHPROCESS
} edosErrorType;

typedef enum
{
    TIMER_RUNNING, TIMER_STOPPED, TIMER_EXPIRED
} timerStateType;

// A process
typedef struct
{
    void *nextProc;
    char procName[8];
    processStateType state;
    void (*fn)(void);
} processType;

// A timer
typedef struct
{
    void *nextTimer;
    int ticksLeft;
    int ticks;
    timerStateType timerState;
} timerType;

// A memory block
typedef struct
{
    boolean allocated;
    unsigned char data[MEMBLOCK_SIZE];
} memblockType;

// A message queue entry
typedef struct
{
    void *nextMsg; 
    unsigned char msg[MSGLEN];
} msgType;

typedef struct
{
    int end;
    boolean gotCmd;
    char buff[SERBUFFLEN];
} serBuffType;

typedef struct
{
    char *cmdName;
    void (*cmdFn)(void);
} shellCmdType;

// The managed memory pool
volatile memblockType memPool[MEMPOOL_SIZE];

// Linked list of processes
processType *processList;

// Currently running process
processType *runningProcess;

// Linked list of timers
timerType *timerList;

edosErrorType edosErrno;

serBuffType serBuff;

void edosShellPs();

shellCmdType shellCmds[] =
{
    { "ps", edosShellPs },
    { NULL, NULL }
};

unsigned long int ms;

// Application variables
boolean ledOn;
boolean ledTrigger;
int ledMgmtState;
timerType *ledTimer;    // A timer
msgType *ledMsgQueue;   // A message queue

//
// Hardware specific BSP part
//

// Initialse serial port
void bspSerialInit()
{
    Serial.begin(9600);
    delay(5000);
}

// Return true if there's something to read from serial port
boolean bspSerialReady()
{
    if(Serial.available())
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Read next character from serial port
char bspGetchar()
{
    return Serial.read();
}

// Print message on serial port, include PID of running process and newline
void bspPrintln(char *msg)
{
    char txtBuff[80];
    sprintf(txtBuff, "[0x%08lx] %s\n", (unsigned long int)runningProcess, msg);
    Serial.print(txtBuff);
}

// Write value to gpio pin
void bspGpioWrite(int pin, logicLevel l)
{
    char txtBuff[80];

    sprintf(txtBuff, "Write: %d -", pin);
    if(l == HIGH)
    {
        digitalWrite(pin, HIGH);
        strcat(txtBuff, "HIGH");
    }
    else
    {
        digitalWrite(pin, LOW);
        strcat(txtBuff, "LOW");
    }

    bspPrintln(txtBuff);

}

// Return number of milliseconds since start
unsigned long int bspMilliseconds()
{
    return millis();
}

void bspInitTimers()
{
    timerList = NULL;
    ms = bspMilliseconds();
}

void bspInitGpio()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

//
// Operating system functions
//

// Initialise shell process
void edosInitShell()
{
    serBuff.end = 0;
    serBuff.gotCmd = false;
}

// Shell process "ps" command
void edosShellPs()
{
    processType *p;
    char txtBuff[80];

    p = processList;
    while(p != NULL)
    {
        sprintf(txtBuff, "%8s 0x%08lx ", p -> procName, (unsigned long int)p);
        if(p -> state == PROC_RUNNING)
        {
            strcat(txtBuff, "RUNNING");
        }
        else
        {
            strcat(txtBuff, "SUSPENDED");
        }
        bspPrintln(txtBuff);
        p = p -> nextProc;
    }
}

// Shell process
// reads one character per schedule loop
// executes command once a whole line is read in
// commands run to completion before returning to scheduler
// if command takes ages, it should be done as a state machine or start another process or similar to allow scheduler to run
void edosShellProcess()
{   
    int c;
    char inch;
    char txtBuff[80];

    if(serBuff.gotCmd == false)
    {
        if(bspSerialReady() == true)
        {
            inch = bspGetchar();
            if(inch == '\n')
            {
                serBuff.buff[serBuff.end] = '\0';
                serBuff.gotCmd = true;
            }
            else
            {
                if(isprint(inch))
                {
                    serBuff.buff[serBuff.end] = inch;
                    serBuff.end++;
                    if(serBuff.end == SERBUFFLEN)
                    {
                        serBuff.end = 0;
                    }
                }
            }
        }
    }
    else
    {
        c = 0;
        while(shellCmds[c].cmdFn != NULL && strcmp(shellCmds[c].cmdName, serBuff.buff) != 0)
        {
            c++;    
        }

        if(shellCmds[c].cmdFn == NULL)
        {
            bspPrintln("Bad command");
        }
        else
        {
            shellCmds[c].cmdFn();
        }

        serBuff.end = 0;
        serBuff.gotCmd = false;
    }
}

// Timer handler - run from scheduler when at least OS_TICK ms have elapsed
void edosHandleTimers()
{
    timerType *t;

    // Service each timer
    t = timerList;
    while(t != NULL)
    {
        if(t -> timerState == TIMER_RUNNING)
        {
            t -> ticksLeft = t -> ticksLeft - 1;
            if(t -> ticksLeft == 0)
            {
                t -> timerState = TIMER_EXPIRED;
            }
        }

        t = (timerType *)(t -> nextTimer);
    }
}

// Show memory allocation
void edosShowMem()
{
    char txtBuff[80];
    int freeBlocks;
    int c;

    sprintf(txtBuff, "Memory pool is %d blocks of %d bytes (%d total)", MEMPOOL_SIZE, MEMBLOCK_SIZE, MEMPOOL_SIZE * MEMBLOCK_SIZE);
    bspPrintln(txtBuff);

    sprintf(txtBuff, "First block is at 0x%04lx", (unsigned long int)memPool[0].data);
    bspPrintln(txtBuff);

    freeBlocks = 0;
    for(c = 0; c < MEMPOOL_SIZE; c++)
    {
        if(memPool[c].allocated == false)
        {
            freeBlocks++;
        }
    }

    sprintf(txtBuff, "Free blocks: %d", freeBlocks);
    bspPrintln(txtBuff);
}

// Free all memory blocks in pool
void edosFreeAll()
{
    int c;

    for(c = 0; c < MEMPOOL_SIZE; c++)
    {
        memPool[c].allocated = false;
        memset((unsigned char *)memPool[c].data, 0, MEMBLOCK_SIZE);
    }
}

// Allocate a memory block
// Return pointer to data part of allocated block or NULL
unsigned char *edosMalloc()
{
    int c;
    unsigned char *rtn;

    c = 0;
    while(c < MEMPOOL_SIZE && memPool[c].allocated == true)
    {
        c++;
    }

    if(c == MEMPOOL_SIZE)
    {
        bspPrintln("Memory allocation error");
        edosErrno = E_OUTOFMEM;
        rtn = NULL;
    }
    else
    {
        memPool[c].allocated = true;
        
        rtn = (unsigned char *)memPool[c].data;
    }

    return rtn;
}

// Free previously allocated memory block
boolean edosFree(unsigned char *block)
{
    int c;
    boolean rtn;

    rtn = true;

    c = 0;
    while(c < MEMPOOL_SIZE && (unsigned char *)memPool[c].data != block)
    {
        c++;
    }

    if(c == MEMPOOL_SIZE || memPool[c].allocated == false)
    {
        bspPrintln("Failed to free memory");
        edosErrno = E_NOSUCHBLOCK;
        rtn = false;
    }
    else
    {
        memPool[c].allocated = false;
        edosErrno = E_OK;
    }

    return rtn;
}

// Create a new timer
// Return pointer to new timer or NULL
timerType *edosCreateTimer(int ticks)
{
    timerType *newTimer;
    timerType *t;
    
    newTimer = (timerType *)edosMalloc();
    if(newTimer != NULL)
    {
        newTimer -> nextTimer = NULL;
        newTimer -> ticks = ticks;
        newTimer -> timerState = TIMER_STOPPED;

        if(timerList == NULL)
        {
            timerList = newTimer;
        }
        else
        {
            t = timerList;
            while(t -> nextTimer != NULL)
            {
                t = (timerType *)(t -> nextTimer);
            }

            t -> nextTimer = newTimer;

            edosErrno = E_OK;
        }
    }

    return newTimer;
}

// Delete a timer
// Return true if ok or false if failed
boolean edosDeleteTimer(timerType *timer)
{
    timerType *t;
    boolean rtn;

    rtn = true;

    t = timerList;
    while(t != NULL && (timerType *)(t -> nextTimer) != timer)
    {
        t = (timerType *)(t -> nextTimer);
    }

    if(t == NULL)
    {
        bspPrintln("Error deleting timer");
        edosErrno = E_NOSUCHTIMER;
        rtn = false;
    }
    else
    {
        t -> nextTimer = timer -> nextTimer;
        edosFree((unsigned char *)timer);
    }

    return rtn;
}

// Start a timer
void edosStartTimer(timerType *timer)
{
    timer -> ticksLeft = timer -> ticks;
    timer -> timerState = TIMER_RUNNING;
    edosErrno = E_OK;
}

// Stop a timer
void edosStopTimer(timerType *timer)
{
    timer -> timerState = TIMER_STOPPED;
    edosErrno = E_OK;
}

// See if a timer has expired
// Return true if timer expired or false if not
boolean edosTimerExpired(timerType *timer)
{
    edosErrno = E_OK;

    if(timer -> timerState == TIMER_EXPIRED)
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Create a new process
// Return pointer to process information
processType *edosCreateProcess(char *procName, void (*fn)(void), processStateType initState)
{
    char txtBuff[80];
    processType *newProc;
    processType *p;

    sprintf(txtBuff, "Creating process %s", procName);
    bspPrintln(txtBuff);

    // Get a memory block from the pool
    newProc = (processType *)edosMalloc();
    if(newProc != NULL)
    {
        edosErrno = E_OK;

        sprintf(txtBuff, " - PID = 0x%08lx", (unsigned long int)newProc);
        bspPrintln(txtBuff);

        // Fill in process descriptor
        newProc -> nextProc = NULL;
        strncpy(newProc -> procName, procName, 7);
        newProc -> state = initState;
        newProc -> fn = fn;

        // If there's nothing in the process list, then this is the start of the list
        if(processList == NULL)
        {
            processList = newProc;
        }
        else
        {
            // Find the end of the list and add new process to it
          
            p = processList;
            while(p -> nextProc != NULL)
            {
                p = (processType *)(p -> nextProc);
            }

             p -> nextProc = newProc;
        }
    }

    return newProc;
}

// Suspend a process
void edosSuspendProcess(processType *proc)
{
    edosErrno = E_OK;
    proc -> state = PROC_SUSPENDED;
}

// Delete a process
// Return true if process deleted or false if not
boolean edosDeleteProcess(processType *proc)
{
    processType *p;
    boolean rtn;

    rtn = true;

    p = processList;
    while(p != NULL && (processType *)(p -> nextProc) != proc)
    {
        p = (processType *)(p -> nextProc);
    }

    if(p == NULL)
    {
        bspPrintln("Error deleting process");
        edosErrno = E_NOSUCHPROCESS;
        rtn = false;
    }
    else
    {
        p -> nextProc = proc -> nextProc;
        rtn = edosFree((unsigned char *)proc);
    }

    return rtn;
}

void edosInitMsgQ(msgType **q)
{
    *q = NULL;
}

// Send a message on message queue
// Return true if ok, false if not
boolean edosSendMsg(msgType **q, unsigned char *buff)
{
    msgType *msg;
    msgType *msgQ;

    msg = (msgType *)edosMalloc();
    if(msg == NULL)
    {
        return false;
    }

    memcpy(msg -> msg, buff, MSGLEN);
    msg -> nextMsg = NULL;

    msgQ = *q;
    if(msgQ == NULL)
    {
        *q = msg;
    }
    else
    {
        while(msgQ -> nextMsg != NULL)
        {
            msgQ = (msgType *)(msgQ -> nextMsg);
        }

        msgQ -> nextMsg = msg;
    }

    edosErrno = E_OK;

    return true;
}

// Read first message off a message queue
// return true if got message, false if no messages on queue
boolean edosReadMsg(msgType **q, unsigned char *buff)
{
    msgType *msg;

    msg = *q;

    if(msg == NULL)
    {
        return false;
    }

    memcpy(buff, msg -> msg, MSGLEN);

    edosFree((unsigned char *)msg);

    if(msg -> nextMsg == NULL)
    {
        *q = NULL;
    }
    else
    {
        *q = (msgType *)(msg -> nextMsg);
    }

    return true;
}

// OS main loop
void edosScheduler()
{
    // If running process is not suspended, then run it
    if(runningProcess -> state == PROC_RUNNING)
    {
        runningProcess -> fn();
    }

    // Schedule next process
    if(runningProcess -> nextProc == NULL)
    {
        runningProcess = processList;
    }
    else
    {
        runningProcess = (processType *)(runningProcess -> nextProc);
    }

    // If enough milliseconds have elapsed, then update the timers
    if(bspMilliseconds() - ms > OS_TICK)
    {
        ms = bspMilliseconds();
        edosHandleTimers();
    }
}

// OS Initialisation
void edosInit()
{
    processType *p;

    bspSerialInit();
    bspPrintln("\n*** edOS starting ***\n");

    bspInitGpio();

    // Initialise timers
    bspInitTimers();

    // Initialise process list
    processList = (processType *)NULL;


    // Initialise memory pool
    edosFreeAll();
    edosShowMem();
    
    edosInitShell();
}

//
// Application processes
//

// Turn the LED on process
void ledOnProcess()
{
    char msg[MSGLEN];

    if(ledTrigger == false)
    {
        return;
    }

    if(ledOn == true)
    {
        ledTrigger = false;
        bspGpioWrite(LED_BUILTIN, LOGHIGH);
        strcpy(msg, "msg: LED ON");
        edosSendMsg(&ledMsgQueue, (unsigned char *)msg);
    }
}

// Turn the LED off process
void ledOffProcess()
{
    char msg[MSGLEN];

    if(ledTrigger == false)
    {
        return;
    }

    if(ledOn == false)
    {
        ledTrigger = false;
        bspGpioWrite(LED_BUILTIN, LOGLOW);
        strcpy(msg, "msg: LED OFF");
        edosSendMsg(&ledMsgQueue, (unsigned char *)msg);
    }
}

// LED management process
void ledMgmtProcess()
{
    char msg[MSGLEN];

    switch(ledMgmtState)
    {
        case LED_INIT:
            edosInitMsgQ(&ledMsgQueue);
            ledTimer = edosCreateTimer(20);
            edosStartTimer(ledTimer);
            ledMgmtState = LED_OFF;
            break;

        case LED_ON:
            if(edosTimerExpired(ledTimer) == true)
            {
                ledTrigger = true;
                ledOn = true;
                ledMgmtState = LED_OFF;
                edosStartTimer(ledTimer);        
            }
            break;

        case LED_OFF:
            if(edosTimerExpired(ledTimer) == true)
            {
                ledTrigger = true;
                ledOn = false;
                ledMgmtState = LED_ON;
                edosStartTimer(ledTimer);        
            }
            break;
    }

    if(edosReadMsg(&ledMsgQueue, (unsigned char *)msg) == true)
    {
        bspPrintln(msg);
    }
}

// Initialisation
void sysInit()
{
    processType *p;

    // Create some processes ready to run
    // Use first process ID to initialise scheduler
    p = edosCreateProcess("LEDON", ledOnProcess, PROC_RUNNING);
    edosCreateProcess("LEDOFF", ledOffProcess, PROC_RUNNING);
    edosCreateProcess("LEDMAN", ledMgmtProcess, PROC_RUNNING);
    edosCreateProcess("SHELL", edosShellProcess, PROC_RUNNING);

    bspPrintln("Running processes...");

    // Schedule first process to run
    runningProcess = p;
}


void setup()
{
    edosInit();
    sysInit(); 
}

void loop()
{
    edosScheduler();
}

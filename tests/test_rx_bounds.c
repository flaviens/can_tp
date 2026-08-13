#include <assert.h>
#include <string.h>

#include "cantp.h"

#define RX_ID 0x123
#define TX_ID 0x456

static uint8_t last_tx[CANTP_FRAME_BYTE];
static uint32_t tx_count;
static uint8_t received[CANTP_FLOW_BYTE];
static uint32_t received_count;
static uint32_t received_id;
static uint32_t received_size;

static bool_t CanTx(uint32_t id, uint8_t* msg, uint32_t size)
{
    (void)id;
    assert(size <= sizeof(last_tx));
    memcpy(last_tx, msg, size);
    tx_count++;
    return TRUE;
}

static void RxCallback(uint32_t id, uint8_t* msg, uint32_t size)
{
    assert(size <= sizeof(received));
    memcpy(received, msg, size);
    received_count++;
    received_id = id;
    received_size = size;
}

static void Setup(Cantp_HandlerStruct* handler)
{
    memset(handler, 0, sizeof(*handler));
    memset(last_tx, 0, sizeof(last_tx));
    memset(received, 0, sizeof(received));
    tx_count = 0;
    received_count = 0;
    received_id = 0;
    received_size = 0;
    Cantp_CanRegister(handler, CanTx, NULL);
    Cantp_CanidRegister(handler, 0, TX_ID, RX_ID);
    Cantp_CallbackRegister(handler, 0, NULL, RxCallback);
    Cantp_AbilityRegister(handler, 0, 255);
}

static void TestOversizedMessageIsRejected(void)
{
    struct {
        Cantp_HandlerStruct handler;
        uint8_t guard[32];
    } state;
    uint8_t first[CANTP_FRAME_BYTE] = {0x1F, 0xFF, 1, 2, 3, 4, 5, 6};
    uint8_t consecutive[CANTP_FRAME_BYTE] = {0x21, 7, 8, 9, 10, 11, 12, 13};

    memset(&state, 0, sizeof(state));
    memset(state.guard, 0xA5, sizeof(state.guard));
    Setup(&state.handler);

    Cantp_RxTask(&state.handler, CALL_BACK, RX_ID, first, sizeof(first));

    assert(tx_count == 1);
    assert(last_tx[0] == ((CANTP_FLOWCONTROL_FRAME << 4) | CANTP_FLOW_STATUS_OVERFLOW));
    assert(state.handler.Rxmsg[0].allsize == 0);
    assert(state.handler.Rxmsg[0].size == 0);
    assert(state.handler.Rxmsg[0].multi == FALSE);

    for(uint32_t i = 0; i < 600; i++)
        Cantp_RxTask(&state.handler, CALL_BACK, RX_ID, consecutive, sizeof(consecutive));

    for(uint32_t i = 0; i < sizeof(state.guard); i++)
        assert(state.guard[i] == 0xA5);
    assert(received_count == 0);
}

static void TestMaximumBufferedMessageIsAccepted(void)
{
    Cantp_HandlerStruct handler;
    uint8_t expected[CANTP_FLOW_BYTE];
    uint8_t frame[CANTP_FRAME_BYTE];
    uint32_t offset = CANTP_FRAME_BYTE - 2;
    uint8_t sequence = 1;

    Setup(&handler);
    for(uint32_t i = 0; i < sizeof(expected); i++)
        expected[i] = (uint8_t)i;

    frame[0] = (CANTP_FIRST_FRAME << 4) | (CANTP_FLOW_BYTE >> 8);
    frame[1] = (uint8_t)(CANTP_FLOW_BYTE & 0xFF);
    memcpy(frame + 2, expected, CANTP_FRAME_BYTE - 2);
    Cantp_RxTask(&handler, CALL_BACK, RX_ID, frame, sizeof(frame));

    while(offset < sizeof(expected))
    {
        uint32_t copy_size = sizeof(expected) - offset;
        if(copy_size > CANTP_FRAME_BYTE - 1)
            copy_size = CANTP_FRAME_BYTE - 1;
        memset(frame, 0, sizeof(frame));
        frame[0] = (CANTP_CONSECUTIVE_FRAME << 4) | (sequence & 0x0F);
        memcpy(frame + 1, expected + offset, copy_size);
        Cantp_RxTask(&handler, CALL_BACK, RX_ID, frame, sizeof(frame));
        offset += copy_size;
        sequence++;
    }

    assert(handler.Rxmsg[0].completed == TRUE);
    assert(handler.Rxmsg[0].multi == FALSE);
    assert(handler.Rxmsg[0].size == CANTP_FLOW_BYTE);
    assert(received_count == 1);
    assert(received_id == RX_ID);
    assert(received_size == CANTP_FLOW_BYTE);
    assert(memcmp(received, expected, sizeof(expected)) == 0);
}

static void TestShortFirstFrameIsRejected(void)
{
    Cantp_HandlerStruct handler;
    uint8_t first[CANTP_FRAME_BYTE] = {0x10, 0x07, 1, 2, 3, 4, 5, 6};
    uint8_t expected[CANTP_FLOW_BYTE];

    Setup(&handler);
    memset(handler.Rxmsg[0].payload, 0xA5, sizeof(handler.Rxmsg[0].payload));
    memcpy(expected, handler.Rxmsg[0].payload, sizeof(expected));

    Cantp_RxTask(&handler, CALL_BACK, RX_ID, first, sizeof(first));

    assert(tx_count == 0);
    assert(handler.Rxmsg[0].allsize == 0);
    assert(handler.Rxmsg[0].size == 0);
    assert(handler.Rxmsg[0].multi == FALSE);
    assert(memcmp(handler.Rxmsg[0].payload, expected, sizeof(expected)) == 0);
    assert(received_count == 0);
}

static void TestInvalidRunningSizeIsRejected(void)
{
    Cantp_HandlerStruct handler;
    uint8_t consecutive[CANTP_FRAME_BYTE] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    uint8_t expected[CANTP_FLOW_BYTE];

    Setup(&handler);
    memset(handler.Rxmsg[0].payload, 0xA5, sizeof(handler.Rxmsg[0].payload));
    memcpy(expected, handler.Rxmsg[0].payload, sizeof(expected));
    handler.Rxmsg[0].allsize = 10;
    handler.Rxmsg[0].size = 11;
    handler.Rxmsg[0].multi = TRUE;

    Cantp_RxTask(&handler, CALL_BACK, RX_ID, consecutive, sizeof(consecutive));

    assert(tx_count == 1);
    assert(last_tx[0] == ((CANTP_FLOWCONTROL_FRAME << 4) | CANTP_FLOW_STATUS_OVERFLOW));
    assert(handler.Rxmsg[0].allsize == 0);
    assert(handler.Rxmsg[0].size == 0);
    assert(handler.Rxmsg[0].multi == FALSE);
    assert(memcmp(handler.Rxmsg[0].payload, expected, sizeof(expected)) == 0);
}

int main(void)
{
    TestOversizedMessageIsRejected();
    TestMaximumBufferedMessageIsAccepted();
    TestShortFirstFrameIsRejected();
    TestInvalidRunningSizeIsRejected();
    return 0;
}

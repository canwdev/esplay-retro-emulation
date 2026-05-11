#include "event.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "gamepad.h"

#define EVENT_QUEUE_LEN 16

static QueueHandle_t s_event_q;
static input_gamepad_state s_prev_pad;

void event_init(void)
{
	s_event_q = xQueueCreate(EVENT_QUEUE_LEN, sizeof(event_t));
	memset(&s_prev_pad, 0, sizeof(s_prev_pad));
	gamepad_read(&s_prev_pad);
}

void event_deinit(void)
{
	if (s_event_q) {
		vQueueDelete(s_event_q);
		s_event_q = NULL;
	}
}

void push_event(const event_t *ev)
{
	if (!s_event_q || !ev) {
		return;
	}
	(void)xQueueSend(s_event_q, ev, 0);
}

int wait_event(event_t *out)
{
	if (!out) {
		return -1;
	}

	for (;;) {
		if (xQueueReceive(s_event_q, out, pdMS_TO_TICKS(20)) == pdTRUE) {
			return 0;
		}

		input_gamepad_state cur;
		gamepad_read(&cur);
		if (memcmp(&cur.values, &s_prev_pad.values, sizeof(cur.values)) != 0) {
			out->type = EVENT_TYPE_KEYPAD;
			out->keypad.state = cur;
			out->keypad.last_state = s_prev_pad;
			s_prev_pad = cur;
			return 0;
		}
	}
}

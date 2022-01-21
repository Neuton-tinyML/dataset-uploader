#ifndef SENDER_FSM_H
#define SENDER_FSM_H

#include "sender.h"

void state_transition(Sender* sender, SenderState state);
void state_transition_delayed(Sender* sender, SenderState state, uint32_t delay);
void sender_fsm(Sender* sender, uv_timer_t* timer, void* buffer, size_t size);

#endif // SENDER_FSM_H

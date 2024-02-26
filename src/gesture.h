#pragma once
#ifndef GESTURE

#include "globals.h"
#include "Pedal.h"

namespace amc
{
	// sequence of M controls performing N actions
	template <size_t M, size_t N> class Sequence
	{
	public:
		Sequence(Pedal* hardware, Pedal::SwitchI* ids, bool* initial, Pedal::SwitchI* changes)
		{
			this->hardware = hardware;
			this->ids = ids;
			this->initial = initial;
			this->changes = changes;

			memset(changed, false, M * sizeof(bool));
		}

		// update internal states
		void tick()
		{
			// update the state corresponding to each control
			for (size_t i = 0; i < M; i++)
			{
				bool before = states[i];
				states[i] = hardware->switches[ids[i]].Pressed();
				changed[i] = (states[i] != before);
			}
		}

		// attempt to step forward in the sequence of controls
		bool step()
		{
			if (position == -1)
			{
				for (size_t i = 0; i < M; i++)
					if (states[i] != initial[i])
						return false;

				position++;
			}

			bool ready = false;
			for (size_t i = 0; i < M; i++)
			{
				// if the i^th button has changed state
				if (changed[i])
				{
					if (i == changes[position])
						ready = true;
					else // wrong button pressed
					{
						position = -1; // square 1
						return false;
					}
				}
			}

			if (ready) // correct button pressed
				position++;
			else // there were no button presses. return false, but no backtracking
				return false;

			if (position == N) // the sequence was completed
			{
				position = -1; // back to square one
				return true;
			}
			else // still working, but no mistakes
				return false;
		}


	private:
		Pedal* hardware;
		Pedal::SwitchI* ids;
		bool* initial;
		Pedal::SwitchI* changes;

		bool states[M];
		bool changed[M];

	public:
		int position = -1;
	};
}



#define GESTURE
#endif


// #pragma once
// #ifndef GESTURE

// #include "globals.h"
// #include "Pedal.h"

// /*
// A gesture is the specification of a set C of controls (e.g., a set of knobs, switches, ...),
// and then a finite sequence A of actions, each of which is of the form (c, a), where c in C and
// a is a boolean function of the current and past states of c (e.g., if c = Switch, rising edge; if
// c = Pot, passed across some interval).  

// A gesture has been performed if each of (c_1, a_1), (c_2, a_2), ..., (c_n, a_n) has been performed in
// sequence, and along the way there have been no state changes among any other elements of C. 
// */
// namespace amc
// {
// 	namespace Gesture
// 	{
// 		enum Interface
// 		{
// 			Knob,
// 			Switch,
// 			Encoder
// 		};

// 		struct Control
// 		{
// 			Interface type;
// 			int identifier;

// 			bool operator==(Control& other)
// 			{
// 				return type == other.type && identifier == other.identifier;
// 			}

// 			bool operator!=(Control& other)
// 			{
// 				return !operator==(other);
// 			}
// 		};

// 		union State
// 		{
// 			struct
// 			{
// 				float value;
// 			} kno;

// 			struct
// 			{
// 				bool now;
// 				bool before;
// 			} swi;

// 			struct
// 			{
// 				bool now;
// 				bool before;
// 				int increment;
// 			} enc;

// 		};

// 		struct Action
// 		{
// 			Control control;
// 			bool (*evaluation)(const Control& control, const State& state);
// 		};

// 		bool RisingEdge(const Control& control, const State& state)
// 		{
// 			switch (control.type)
// 			{
// 			case Interface::Switch:
// 				return state.swi.now && (!state.swi.before);

// 			case Interface::Encoder:
// 				return state.enc.now && (!state.enc.before);

// 			default:
// 				break;
// 			}

// 			return false;
// 		}

// 		bool FallingEdge(const Control& control, const State& state)
// 		{
// 			switch (control.type)
// 			{
// 			case Interface::Switch:
// 				return !state.swi.now && state.swi.before;

// 			case Interface::Encoder:
// 				return !state.enc.now && state.enc.before;

// 			default:
// 				break;
// 			}

// 			return false;
// 		}

// 		enum SequenceState
// 		{
// 			failed,
// 			progressing,
// 			finished
// 		};

// 		// sequence of M controls performing N actions
// 		template <size_t M, size_t N> class Sequence
// 		{
// 		public:
// 			Sequence(Pedal* hw, Control* cs, Action* as)
// 			{
// 				hardware = hw;
// 				controls = cs;
// 				actions = as;
// 			}

// 			// update internal states
// 			void tick()
// 			{
// 				Control* c;
// 				// update the state corresponding to each control
// 				for (size_t i = 0; i < M; i++)
// 				{
// 					c = &controls[i];
// 					switch (c->type)
// 					{
// 					case Knob:
// 						states[i].kno.value = hardware->knobs[c->identifier].Value();
// 						break;

// 					case Switch:
// 						states[i].swi.before = states[i].swi.now;
// 						states[i].swi.now = hardware->switches[c->identifier].Pressed();
// 						break;

// 					case Encoder:
// 						states[i].enc.before = states[i].enc.now;
// 						states[i].enc.now = hardware->encoders[c->identifier].Pressed();
// 						states[i].enc.increment = hardware->encoders[c->identifier].Increment();
// 						break;

// 					default:
// 						break;
// 					}
// 				}
// 			}

// 			// attempt to step forward in the sequence of controls
// 			SequenceState step()
// 			{
// 				if (position == -1)
// 				{
// 					position += 1;
// 					return SequenceState::progressing;
// 				}

// 				size_t index = 0;
// 				while (controls[index] != actions[position].control && index < M)
// 					index++;

// 				// our action called for a control which wasn't provided in the initialiation
// 				if (index == M)
// 					return SequenceState::failed;

// 				// step the position forward if the correct action was taken
// 				bool satisfied = actions[position].evaluation(actions[position].control, states[index]);
// 				position = satisfied ? position + 1 : 0;

// 				if (position == N) // the sequence was completed
// 				{
// 					position = -1;
// 					return SequenceState::finished;
// 				}

// 				if (satisfied)
// 					return SequenceState::progressing;
// 				else
// 					return SequenceState::failed;
// 			}


// 		private:
// 			Pedal* hardware;
// 			Control* controls;
// 			State states[M];
// 			Action* actions;

// 			int position = -1;
// 			bool satisfied = true;
// 		};
// 	}
// }


// #define GESTURE
// #endif

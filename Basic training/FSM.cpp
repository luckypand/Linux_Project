#include <iostream>

class FSM
{
public:
    enum State
    {
        START,
        NUMBER,
        END,
        ERROR
    };
private:
    State state;
public:
    FSM()
        :state(START)
    {

    }

    bool process(char c)
    {
        switch (state)
        {
            case START:
                if(isdigit(c))
                {
                    state = NUMBER;
                }
                else
                {
                    state = ERROR;
                }
                break;
            case NUMBER:
                if(isdigit(c))
                {

                }
                else if(c == '\n')
                {
                    state = END;
                }
                else
                {
                    state = ERROR;
                }
                break;
            case END:
                
                break;
            default:
                break;
        }   
        return state != ERROR;
    }

    bool finish() const
    {
        return state == END;
    }
};
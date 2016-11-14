/*
 * Copyright (c) 2016 PolySync
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <cmath>
#include <iostream>

#include <PolySyncDataModel.hpp>

#include "SearchNode.hpp"
#include "GridMap.hpp"


using namespace cv;
using namespace std;
using namespace polysync::datamodel;


constexpr int INVALID_LOC = -1;

/**
 * \example SearchNode.cpp
 *
 * PolySync Path Planner Example.
 *
 * Shows how to subscribe a simulated planning algorithm node to a simulated
 * robot node.  Search node finds the optimal path from a start location to a
 * generated goal location.  Then the node sends the optimal path one waypoint
 * at a time in the form of platform motion messages.  Specifically, each
 * platform motion message has a position and an orientation.  The outgoing
 * position fields give the current waypoint number and total number of
 * waypoints.  The outgoing orientation fields give the x and y axis coordinate
 * that the robot should move to.
 *
 * @file SearchNode.cpp
 * @brief SearchNode Source
 *
 */
SearchNode::SearchNode( )
    :
    _searcher( ),
    _golLocX( INVALID_LOC ),
    _golLocY( INVALID_LOC ),
    _robLocX( INVALID_LOC ),
    _robLocY( INVALID_LOC ),
    _newRobLocX( INVALID_LOC ),
    _newRobLocY( INVALID_LOC ),
    _numWaypoints( INVALID_LOC ),
    _waypointCounter( INVALID_LOC )
{
    setNodeName( "searchNode" );

    setNodeType( PSYNC_NODE_TYPE_SOFTWARE_ALGORITHM );
}

SearchNode::~SearchNode( )
{
}

/**
 * @brief initStateEvent
 *
 * The initStateEvent is triggered once when this node is initialized in
 * PolySync. This is a good place to initialize variables dependant on a
 * polysync::Node reference.
 *
 */
void SearchNode::initStateEvent( )
{

    // register node as a subscriber to platform motion messages from ANY node.
    registerListener( getMessageTypeByName( "ps_platform_motion_msg" ) );

    setSubscriberReliabilityQOS(
            getMessageTypeByName( "ps_platform_motion_msg" ),
            RELIABILITY_QOS_RELIABLE );
}

/**
 * @brief okStateEvent
 *
 * Override the base class functionality to send messages when the node
 * reaches the "ok" state. This is the state where the node is in its
 * normal operating mode.
 *
 */
void SearchNode::okStateEvent( )
{

    // generate goal state at a pseudo-random location.
    if ( _golLocX == INVALID_LOC && _golLocY == INVALID_LOC )
    {

        _searcher = std::unique_ptr< Planner >{ new Planner };

        _golLocX = _searcher->getGoalX( );
        _golLocY = _searcher->getGoalY( );

        cout << endl << "Goal Location generated by Planner Algorithm. ";
        cout << endl << "Sending goal location to robot." << endl << endl;
        cout << "Waiting for Robot Location." << endl << endl << std::flush;

    }

    // send goal location to robot repeatedly until it is received.
    else if ( _robLocX == INVALID_LOC || _robLocY == INVALID_LOC )
    {

        sendGoalToRobot( );

        // do nothing, sleep for 10 milliseconds
        polysync::sleepMicro(10000);

    }

    // once robot reports its starting location, search the space for the
    // optimal path from start to goal state.
    else if ( _newRobLocX == INVALID_LOC && _newRobLocY == INVALID_LOC )
    {

        cout << "Robot start location received by planner algorithm." << endl;
        cout << "Begin searching for optimal path from start location." << endl;

        int robIndex = _searcher->world.getIndexFromState( _robLocX, _robLocY) ;

        // use A* search to find optimal path.
        _numWaypoints = _searcher->searchAStar( robIndex );

        _newRobLocX = int(_robLocX);
        _newRobLocY = int(_robLocY);

    }

    // wait until done searching, then send out next waypoint.
    else if ( _newRobLocX != INVALID_LOC || _newRobLocY != INVALID_LOC )
    {

        // have I sent the final waypoint?  if so, shut down
        if ( _waypointCounter == _numWaypoints - 2 )
        {
            cout << endl << "Robot arrived at goal state after ";
            cout << _waypointCounter << " waypoints. " <<  endl;
            cout << "Shutting down SearchNode." << endl << endl;

            disconnectPolySync( );

            return;
        }

        cout << "Sending waypoint " << _waypointCounter + 1 << " to robot.";
        cout << endl;

        int newIndex = _searcher->getNextWaypoint( _waypointCounter + 1 );

        sendNextWaypoint( newIndex, int(_waypointCounter + 1) );

        // The ok state is called periodically by the system so sleep to reduce
        // the number of messages sent. do nothing, sleep for 1 millisecond.
        polysync::sleepMicro(1000);

    }

    else
    {

        // do nothing, sleep for 100 milliseconds
        polysync::sleepMicro(100000);
        return;

    }
}

/**
 * @brief messageEvent
 *
 * Extract the information from the provided message
 *
 * @param std::shared_ptr< Message > - variable containing the message
 */
void SearchNode::messageEvent( std::shared_ptr< polysync::Message > newMsg )
{

    // check whether new message is not your own. This check is only important
    // since robotNode and searchNode both publish and subscribe to messages.
    if ( newMsg->getSourceGuid( ) == getGuid( ) )
    {
        return;
    }

    // now check whether new message is a PlatformMotionMessage.
    if ( auto msg = getSubclass< PlatformMotionMessage >( newMsg ) )
    {

        // all received platform motion messages will be current robot location.
        // robot will also report back last waypoint received so planner can
        // send the next waypoint.
        if ( msg->getOrientation()[0] != _robLocX ||
                msg->getOrientation()[1] != _robLocY )
        {
            _robLocX = msg->getOrientation()[0];
            _robLocY = msg->getOrientation()[1];

            if ( _waypointCounter != INVALID_LOC )
            {
                cout << "New Robot Location Message received at waypoint: ";
                cout << msg->getPosition()[0] << endl << std::flush;
            }
            _waypointCounter = msg->getPosition()[0];
        }
    }
}

/**
 * @brief sendGoalToRobot
 *
 * SearchNode reports goal location so that RobotNode can render the GridMap.
 *
 */
void SearchNode::sendGoalToRobot( )
{

    // Create a message
    PlatformMotionMessage msg( *this );

    // Set publish time
    msg.setHeaderTimestamp( polysync::getTimestamp() );

    // Populate buffer
    msg.setOrientation( { double(_golLocX), double(_golLocY), 0, 0 } );

    // Publish to the PolySync bus
    msg.publish( );

}

/**
 * @brief sendNextWaypoint
 *
 * Receive current robot location and send the next waypoint from there.
 *
 * @param int, int - index of new position, # of waypoint at that position
 */
void SearchNode::sendNextWaypoint( int newIndex, int waypointID )
{

    _searcher->world.getStateFromIndex( newIndex );
    _newRobLocX = _searcher->world.checkedMoveIndX;
    _newRobLocY = _searcher->world.checkedMoveIndY;

    // Create a message
    PlatformMotionMessage msg( *this );

    // Set publish time
    msg.setHeaderTimestamp( polysync::getTimestamp() );

    // Populate buffer
    msg.setPosition( { double(waypointID), 0, double(_numWaypoints) } );
    msg.setOrientation( { _newRobLocX, _newRobLocY, 0, 0 } );

    // Publish to the PolySync bus
    msg.publish();

}

/**
 * Entry point for the SearchNode (planner) side of this tutorial application.
 * The node will search the map and generate a set waypoints to send to the
 * robot node. The "connectPolySync" is a blocking call, users must use Ctrl-C
 * to exit this function.
 */

int main()
{

    SearchNode searchNode;
    searchNode.connectPolySync( );

    return 0;
}

//////////////////////////////////////////////////////////////////////////
//  
//  Copyright (c) 2013-2014, Image Engine Design Inc. All rights reserved.
//  
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//  
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//  
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//  
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//  
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//  
//////////////////////////////////////////////////////////////////////////

#include "boost/python.hpp"

#include "Gaffer/Context.h"
#include "Gaffer/Dispatcher.h"
#include "Gaffer/CompoundPlug.h"

#include "GafferBindings/DispatcherBinding.h"
#include "GafferBindings/NodeBinding.h"
#include "GafferBindings/SignalBinding.h"

using namespace boost::python;
using namespace IECore;
using namespace IECorePython;
using namespace Gaffer;
using namespace GafferBindings;

namespace
{

class DispatcherWrapper : public NodeWrapper<Dispatcher>
{
	public :

		DispatcherWrapper( PyObject *self, const std::string &name )
			: NodeWrapper<Dispatcher>( self, name )
		{
		}

		virtual ~DispatcherWrapper()
		{
		}

		void dispatch( list nodeList ) const
		{
			ScopedGILLock gilLock;
			size_t len = boost::python::len( nodeList );
			std::vector<ExecutableNodePtr> nodes;
			nodes.reserve( len );
			for ( size_t i = 0; i < len; i++ )
			{
				nodes.push_back( extract<ExecutableNodePtr>( nodeList[i] ) );
			}
			Dispatcher::dispatch( nodes );
		}

		void doDispatch( const TaskDescriptions &taskDescriptions ) const
		{
			ScopedGILLock gilLock;
			
			list taskList;
			for ( TaskDescriptions::const_iterator tIt = taskDescriptions.begin(); tIt != taskDescriptions.end(); ++tIt )
			{
				list requirements;
				for ( std::set<ExecutableNode::Task>::const_iterator rIt = tIt->requirements.begin(); rIt != tIt->requirements.end(); ++rIt )
				{
					requirements.append( *rIt );
				}
				
				taskList.append( make_tuple( tIt->task, requirements ) );
			}
			
			boost::python::object f = this->methodOverride( "_doDispatch" );
			if( f )
			{
				f( taskList );
			}
			else
			{
				throw Exception( "doDispatch() python method not defined" );
			}
		}

		void doSetupPlugs( CompoundPlug *parentPlug ) const
		{
			ScopedGILLock gilLock;
			boost::python::object f = this->methodOverride( "_doSetupPlugs" );
			if( f )
			{
				CompoundPlugPtr tmpPointer = parentPlug;
				f( tmpPointer );
			}
		}

		static list dispatcherNames()
		{
			std::vector<std::string> names;
			Dispatcher::dispatcherNames( names );
			list result;
			for ( std::vector<std::string>::const_iterator nIt = names.begin(); nIt != names.end(); nIt++ )
			{
				result.append( *nIt );
			}
			return result;
		}

		static void registerDispatcher( std::string name, Dispatcher *dispatcher )
		{
			Dispatcher::registerDispatcher( name, dispatcher );
		}

		static DispatcherPtr dispatcher( std::string name )
		{
			const Dispatcher *d = Dispatcher::dispatcher( name );
			return const_cast< Dispatcher *>(d);
		}

};

struct DispatchSlotCaller
{
	boost::signals::detail::unusable operator()( boost::python::object slot, const Dispatcher *d, const std::vector<ExecutableNodePtr> &nodes )
	{
		try
		{
			list nodeList;
			for( std::vector<ExecutableNodePtr>::const_iterator nIt = nodes.begin(); nIt != nodes.end(); nIt++ )
			{
				nodeList.append( *nIt );
			}
			DispatcherPtr dd = const_cast<Dispatcher*>(d);
			slot( dd, nodeList );
		}
		catch( const error_already_set &e )
		{
			PyErr_PrintEx( 0 ); // clears the error status
		}
		return boost::signals::detail::unusable();
	}
};

} // namespace

void GafferBindings::bindDispatcher()
{
	scope s = NodeClass<Dispatcher, DispatcherWrapper>()
		.def( "dispatch", &DispatcherWrapper::dispatch )
		.def( "jobDirectory", &Dispatcher::jobDirectory )
		.def( "dispatcher", &DispatcherWrapper::dispatcher ).staticmethod( "dispatcher" )
		.def( "dispatcherNames", &DispatcherWrapper::dispatcherNames ).staticmethod( "dispatcherNames" )
		.def( "registerDispatcher", &DispatcherWrapper::registerDispatcher ).staticmethod( "registerDispatcher" )
		.def( "preDispatchSignal", &Dispatcher::preDispatchSignal, return_value_policy<reference_existing_object>() ).staticmethod( "preDispatchSignal" )
		.def( "postDispatchSignal", &Dispatcher::postDispatchSignal, return_value_policy<reference_existing_object>() ).staticmethod( "postDispatchSignal" )
	;
	
	enum_<Dispatcher::FramesMode>( "FramesMode" )
		.value( "CurrentFrame", Dispatcher::CurrentFrame )
		.value( "ScriptRange", Dispatcher::ScriptRange )
		.value( "CustomRange", Dispatcher::CustomRange )
	;
	
	SignalBinder<Dispatcher::DispatchSignal, DefaultSignalCaller<Dispatcher::DispatchSignal>, DispatchSlotCaller >::bind( "DispatchSignal" );
}

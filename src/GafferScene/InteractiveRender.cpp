//////////////////////////////////////////////////////////////////////////
//  
//  Copyright (c) 2013, Image Engine Design Inc. All rights reserved.
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

#include "boost/bind.hpp"

#include "IECore/WorldBlock.h"

#include "Gaffer/Context.h"
#include "Gaffer/ScriptNode.h"

#include "GafferScene/InteractiveRender.h"
#include "GafferScene/RendererAlgo.h"
#include "GafferScene/PathMatcherData.h"
#include "GafferScene/SceneProcedural.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferScene;

IE_CORE_DEFINERUNTIMETYPED( InteractiveRender );

size_t InteractiveRender::g_firstPlugIndex = 0;

InteractiveRender::InteractiveRender( const std::string &name )
	:	Node( name ), m_lightsDirty( true ), m_shadersDirty( true )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new ScenePlug( "in" ) );
	addChild( new IntPlug( "state", Plug::In, Stopped, Stopped, Paused, Plug::Default & ~Plug::Serialisable ) );
	addChild( new BoolPlug( "updateLights", Plug::In, true ) );
	addChild( new BoolPlug( "updateShaders", Plug::In, true ) );

	plugDirtiedSignal().connect( boost::bind( &InteractiveRender::plugDirtied, this, ::_1 ) );
	parentChangedSignal().connect( boost::bind( &InteractiveRender::parentChanged, this, ::_1, ::_2 ) );
	
	setContext( new Context() );
}

InteractiveRender::~InteractiveRender()
{
}

ScenePlug *InteractiveRender::inPlug()
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

const ScenePlug *InteractiveRender::inPlug() const
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *InteractiveRender::statePlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *InteractiveRender::statePlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 1);
}
		
Gaffer::BoolPlug *InteractiveRender::updateLightsPlug()
{
	return getChild<BoolPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::BoolPlug *InteractiveRender::updateLightsPlug() const
{
	return getChild<BoolPlug>( g_firstPlugIndex + 2 );
}

Gaffer::BoolPlug *InteractiveRender::updateShadersPlug()
{
	return getChild<BoolPlug>( g_firstPlugIndex + 3 );
}

const Gaffer::BoolPlug *InteractiveRender::updateShadersPlug() const
{
	return getChild<BoolPlug>( g_firstPlugIndex + 3 );
}

void InteractiveRender::plugDirtied( const Gaffer::Plug *plug )
{
	if(
		plug == inPlug()->transformPlug() ||
		plug == inPlug()->objectPlug() ||
		plug == inPlug()->globalsPlug()
	)
	{
		// just store the fact that something needs
		// updating. we'll do the actual update when
		// the dirty signal is emitted for the parent plug.
		m_lightsDirty = true;
	}
	else if( plug == inPlug()->attributesPlug() )
	{
		// as above.
		m_shadersDirty = true;
		m_lightsDirty = true;
	}
	else if(
		plug == inPlug() ||
		plug == updateLightsPlug() ||
		plug == updateShadersPlug() ||
		plug == statePlug()
	)
	{
		update();
	}
}

void InteractiveRender::parentChanged( GraphComponent *child, GraphComponent *oldParent )
{
	ScriptNode *n = ancestor<ScriptNode>();
	if( n )
	{
		setContext( n->context() );
	}
	else
	{
		setContext( new Context() );
	}
}

void InteractiveRender::update()
{
	Context::Scope scopedContext( m_context );

	const State requiredState = (State)statePlug()->getValue();
	ConstScenePlugPtr requiredScene = inPlug()->getInput<ScenePlug>();
	
	// Stop the current render if it's not what we want,
	// and early-out if we don't want another one.
	
	if( !requiredScene || requiredScene != m_scene || requiredState == Stopped )
	{
		// stop the current render
		m_renderer = NULL;
		m_scene = NULL;
		m_state = Stopped;
		m_lightHandles.clear();
		m_shadersDirty = m_lightsDirty = true;
		if( !requiredScene || requiredState == Stopped )
		{
			return;
		}
	}
	
	// If we've got this far, we know we want to be running or paused.
	// Start a render if we don't have one.
	
	if( !m_renderer )
	{
		m_renderer = createRenderer();
		m_renderer->setOption( "editable", new BoolData( true ) );
		
		ConstCompoundObjectPtr globals = inPlug()->globalsPlug()->getValue();
		outputOptions( globals, m_renderer );
		outputCamera( inPlug(), globals, m_renderer );
		{
			WorldBlock world( m_renderer );
		
			outputLightsInternal( globals, /* editing = */ false );

			SceneProceduralPtr proc = new SceneProcedural( inPlug(), Context::current() );
			m_renderer->procedural( proc );
		}
		
		m_scene = requiredScene;
		m_state = Running;
		m_lightsDirty = m_shadersDirty = false;
	}
	
	// Make sure the paused/running state is as we want.
	
	if( requiredState != m_state )
	{
		if( requiredState == Paused )
		{
			m_renderer->editBegin( "suspendrendering", CompoundDataMap() );
		}
		else
		{
			m_renderer->editEnd();
		}
		m_state = requiredState;
	}
	
	// If we're not paused, then send any edits we need.
	
	if( m_state == Running )
	{
		updateLights();
		updateShaders();
	}
}

void InteractiveRender::updateLights()
{
	if( !m_lightsDirty || !updateLightsPlug()->getValue() )
	{
		return;
	}
	IECore::ConstCompoundObjectPtr globals = inPlug()->globalsPlug()->getValue();
	outputLightsInternal( globals.get(), /* editing = */ true );
	m_lightsDirty = false;
}

void InteractiveRender::outputLightsInternal( const IECore::CompoundObject *globals, bool editing )
{
	// Get the paths to all the lights
	const PathMatcherData *lightSet = NULL;
	if( const CompoundData *sets = globals->member<CompoundData>( "gaffer:sets" ) )
	{
		lightSet = sets->member<PathMatcherData>( "__lights" );
	}
	
	std::vector<std::string> lightPaths;
	if( lightSet )
	{
		lightSet->readable().paths( lightPaths );
	}
	
	// Create or update lights in the renderer as necessary
	
	for( vector<string>::const_iterator it = lightPaths.begin(), eIt = lightPaths.end(); it != eIt; ++it )
	{
		ScenePlug::ScenePath path;
		ScenePlug::stringToPath( *it, path );
		
		if( !editing )
		{
			// defining the scene for the first time
			if( outputLight( inPlug(), path, m_renderer ) )
			{
				m_lightHandles.insert( *it );
			}
		}
		else
		{
			if( m_lightHandles.find( *it ) != m_lightHandles.end() )
			{
				// we've already output this light - update it
				m_renderer->editBegin( "light", CompoundDataMap() );
					const bool visible = outputLight( inPlug(), path, m_renderer );
				m_renderer->editEnd();
				// we may have turned it off before, and need to turn
				// it back on, or it may have been hidden and we need
				// to turn it off.
				m_renderer->editBegin( "attribute", CompoundDataMap() );
					m_renderer->illuminate( *it, visible );
				m_renderer->editEnd();
			}
			else
			{
				// we've not seen this light before - create a new one
				m_renderer->editBegin( "attribute", CompoundDataMap() );
					if( outputLight( inPlug(), path, m_renderer ) )
					{
						m_lightHandles.insert( *it );
					}
				m_renderer->editEnd();
			}
		}
	}
	
	// Turn off any lights we don't want any more
	
	for( LightHandles::const_iterator it = m_lightHandles.begin(), eIt = m_lightHandles.end(); it != eIt; ++it )
	{
		if( !lightSet || !(lightSet->readable().match( *it ) & Filter::ExactMatch) )
		{
			m_renderer->editBegin( "attribute", CompoundDataMap() );
				m_renderer->illuminate( *it, false );
			m_renderer->editEnd();
		}
	}
}

void InteractiveRender::updateShaders()
{
	if( !m_shadersDirty || !updateShadersPlug()->getValue() )
	{
		return;
	}
	
	updateShadersWalk( ScenePlug::ScenePath() );
	
	m_shadersDirty = false;
}

void InteractiveRender::updateShadersWalk( const ScenePlug::ScenePath &path )
{
	/// \todo Keep a track of the hashes of the shaders at each path,
	/// and use it to only update the shaders when they've changed.	
	ConstCompoundObjectPtr attributes = inPlug()->attributes( path );
	ConstObjectVectorPtr shader = attributes->member<ObjectVector>( "shader" );
	if( shader )
	{
		std::string name;
		ScenePlug::pathToString( path, name );
		
		CompoundDataMap parameters;
		parameters["exactscopename"] = new StringData( name );
		m_renderer->editBegin( "attribute", parameters );
		
			for( ObjectVector::MemberContainer::const_iterator it = shader->members().begin(), eIt = shader->members().end(); it != eIt; it++ )
			{
				const StateRenderable *s = runTimeCast<const StateRenderable>( it->get() );
				if( s )
				{
					s->render( m_renderer );
				}
			}

		m_renderer->editEnd();
	}
	
	ConstInternedStringVectorDataPtr childNames = inPlug()->childNames( path );
	ScenePlug::ScenePath childPath = path;
	childPath.push_back( InternedString() ); // for the child name
	for( vector<InternedString>::const_iterator it=childNames->readable().begin(); it!=childNames->readable().end(); it++ )
	{
		childPath[path.size()] = *it;
		updateShadersWalk( childPath );
	}
}

Gaffer::Context *InteractiveRender::getContext()
{
	return m_context.get();
}

const Gaffer::Context *InteractiveRender::getContext() const
{
	return m_context.get();
}

void InteractiveRender::setContext( Gaffer::ContextPtr context )
{
	m_context = context;
}

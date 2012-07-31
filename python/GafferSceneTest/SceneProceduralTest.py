##########################################################################
#  
#  Copyright (c) 2012, John Haddon. All rights reserved.
#  
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#  
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#  
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#  
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#  
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#  
##########################################################################

import unittest

import IECore
import IECoreGL

import Gaffer
import GafferTest
import GafferScene
import GafferSceneTest

class SceneProceduralTest( unittest.TestCase ) :

	class __WrappingProcedural( IECore.ParameterisedProcedural ) :
		
		def __init__( self, procedural ) :
		
			IECore.ParameterisedProcedural.__init__( self, "" )
			
			self.__procedural = procedural
			
		def doBound( self, args ) :
		
			return self.__procedural.bound()
			
		def doRender( self, renderer, args ) :
		
			renderer.procedural( self.__procedural )
			
	def testComputationErrors( self ) :
	
		# This test actually exposed a crash bug in IECoreGL, but it's important
		# that Gaffer isn't susceptible to triggering that bug.
	
		mc = GafferScene.ModelCacheSource( inputs = { "fileName" : "iDontExist" } )
		
		renderer = IECoreGL.Renderer()
		renderer.setOption( "gl:mode", IECore.StringData( "deferred" ) )

		with IECore.WorldBlock( renderer ) :
		
			procedural = GafferScene.SceneProcedural( mc["out"], Gaffer.Context(), "/" )
			self.__WrappingProcedural( procedural ).render( renderer )
	
	def testPythonComputationErrors( self ) :
	
		# As above, this may be an IECoreGL bug, but again it's important that
		# Gaffer doesn't trigger it.
		
		script = Gaffer.ScriptNode()
		script["plane"] = GafferScene.Plane()
		
		script["expression"] = Gaffer.ExpressionNode()
		script["expression"]["engine"].setValue( "python" )
		script["expression"]["expression"].setValue( 'parent["plane"]["transform"]["translate"]["x"] = iDontExist["andNorDoI"]' )
				
		renderer = IECoreGL.Renderer()
		renderer.setOption( "gl:mode", IECore.StringData( "deferred" ) )

		with IECore.WorldBlock( renderer ) :
		
			procedural = GafferScene.SceneProcedural( script["plane"]["out"], Gaffer.Context(), "/" )
			self.__WrappingProcedural( procedural ).render( renderer )
		
if __name__ == "__main__":
	unittest.main()
##########################################################################
#  
#  Copyright (c) 2011-2012, John Haddon. All rights reserved.
#  Copyright (c) 2011-2013, Image Engine Design Inc. All rights reserved.
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

import ast
import sys
import traceback

import IECore

import Gaffer
from Gaffer import ScriptNode
import GafferUI
from Menu import Menu
from Widget import Widget
from EditorWidget import EditorWidget

QtGui = GafferUI._qtImport( "QtGui" )
QtCore = GafferUI._qtImport( "QtCore" )

## \todo Custom right click menu with script load, save, execute file, undo, redo etc.
## \todo Standard way for users to customise all menus
## \todo Tab completion and popup help. rlcompleter module should be useful for tab completion. Completer( dict ) constructs a completer
# that works in a specific namespace.
class ScriptEditor( GafferUI.EditorWidget ) :

	def __init__( self, scriptNode, **kw ) :
	
		self.__splittable = GafferUI.SplitContainer()
		
		GafferUI.EditorWidget.__init__( self, self.__splittable, scriptNode, **kw )
			
		self.__outputWidget = GafferUI.MultiLineTextWidget( editable = False, wrapMode = GafferUI.MultiLineTextWidget.WrapMode.None )			
		self.__inputWidget = GafferUI.MultiLineTextWidget( wrapMode = GafferUI.MultiLineTextWidget.WrapMode.None )
		
		self.__splittable.append( self.__outputWidget )
		self.__splittable.append( self.__inputWidget )
	
		self.__inputWidgetActivatedConnection = self.__inputWidget.activatedSignal().connect( Gaffer.WeakMethod( self.__activated ) )
		self.__inputWidgetDropTextConnection = self.__inputWidget.dropTextSignal().connect( Gaffer.WeakMethod( self.__dropText ) )
	
		self.__executionDict = {
			"IECore" : IECore,
			"Gaffer" : Gaffer,
			"GafferUI" : GafferUI,
			"script" : scriptNode,
			"parent" : scriptNode
		}
	
	def __repr__( self ) :

		return "GafferUI.ScriptEditor( scriptNode )"
				
	def __activated( self, widget ) :
		
		# decide what to execute
		haveSelection = True
		toExecute = widget.selectedText()
		if not toExecute :
			haveSelection = False
			toExecute = widget.getText()
		
		# parse it first. this lets us give better error formatting
		# for syntax errors, and also figure out whether we can eval()
		# and display the result or must exec() only.
		try :
			parsed = ast.parse( toExecute )
		except SyntaxError, e :
			self.__outputWidget.appendHTML( self.__syntaxErrorToHTML( e ) )
			return
		
		# execute it
				
		self.__outputWidget.appendHTML( self.__codeToHTML( toExecute ) )

		with Gaffer.OutputRedirection( stdOut = Gaffer.WeakMethod( self.__redirectOutput ), stdErr = Gaffer.WeakMethod( self.__redirectOutput ) ) :
			with _MessageHandler( self.__outputWidget ) :
				with Gaffer.UndoContext( self.scriptNode() ) :
					try :
						if len( parsed.body ) == 1 and isinstance( parsed.body[0], ast.Expr ) :
							result = eval( toExecute, self.__executionDict, self.__executionDict )
							if result is not None :
								self.__outputWidget.appendText( str( result ) )
						else :
							exec( toExecute, self.__executionDict, self.__executionDict )
						if not haveSelection :
							widget.setText( "" )
					except Exception, e :
						self.__outputWidget.appendHTML( self.__exceptionToHTML() )

		return True

	def __dropText( self, widget, dragData ) :
						
		if isinstance( dragData, IECore.StringVectorData ) :
			return repr( list( dragData ) )
		elif isinstance( dragData, Gaffer.GraphComponent ) :
			if self.scriptNode().isAncestorOf( dragData ) :
				return "script['" + dragData.relativeName( self.scriptNode() ).replace( ".", "']['" ) + "']"
		elif isinstance( dragData, Gaffer.Set ) :
			if len( dragData ) == 1 :
				return self.__dropText( widget, dragData[0] )
			else :
				return "[ " + ", ".join( [ self.__dropText( widget, d ) for d in dragData ] ) + " ]"
		elif isinstance( dragData, IECore.Data ) and hasattr( dragData, "value" ) :
			return repr( dragData.value )
			
		return None
		
	def __codeToHTML( self, code ) :
	
		code = code.replace( "<", "&lt;" ).replace( ">", "&gt;" )
		return "<pre>" + code + "</pre>"
	
	def __syntaxErrorToHTML( self, syntaxError ) :
	
		formatted = traceback.format_exception_only( SyntaxError, syntaxError )
		lineNumber = formatted[0].rpartition( "," )[2].strip()
		headingText = formatted[-1].replace( ":", " : " + lineNumber + " : ", 1 )
		result = "<h1 class='ERROR'>%s</h1>" % headingText
		result += "<br>" + self.__codeToHTML( "".join( formatted[1:-1] ) )
		
		return result
	
	def __exceptionToHTML( self ) :
	
		t = traceback.extract_tb( sys.exc_info()[2] )
		lineNumber = str( t[1][1] )
		headingText = traceback.format_exception_only( *(sys.exc_info()[:2]) )[0].replace( ":", " : line " + lineNumber + " : ", 1 )
		result = "<h1 class='ERROR'>%s</h1>" % headingText
		if len( t ) > 2 :
			result += "<br>" + self.__codeToHTML( "".join( traceback.format_list( t[2:] ) ) )
		
		return result
	
	def __redirectOutput( self, output ) :
		
		if output != "\n" :
			self.__outputWidget.appendText( output )
			# update the gui so messages are output as they occur, rather than all getting queued
			# up till the end.
			QtGui.QApplication.instance().processEvents( QtCore.QEventLoop.ExcludeUserInputEvents )

GafferUI.EditorWidget.registerType( "ScriptEditor", ScriptEditor )

class _MessageHandler( IECore.MessageHandler ) :

	def __init__( self, textWidget ) :
	
		IECore.MessageHandler.__init__( self )
		
		self.__textWidget = textWidget
		
	def handle( self, level, context, message ) :
	
		html = formatted = "<h1 class='%s'>%s : %s </h1><span class='message'>%s</span><br>" % ( 
			IECore.Msg.levelAsString( level ),
			IECore.Msg.levelAsString( level ),
			context,
			message.replace( "\n", "<br>" )
		)
		self.__textWidget.appendHTML( html )
		# update the gui so messages are output as they occur, rather than all getting queued
		# up till the end.
		QtGui.QApplication.instance().processEvents( QtCore.QEventLoop.ExcludeUserInputEvents )

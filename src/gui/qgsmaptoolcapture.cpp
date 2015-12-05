/***************************************************************************
    qgsmaptoolcapture.cpp  -  map tool for capturing points, lines, polygons
    ---------------------
    begin                : January 2006
    copyright            : (C) 2006 by Martin Dobias
    email                : wonder.sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsmaptoolcapture.h"

#include "qgscursors.h"
#include "qgsgeometryvalidator.h"
#include "qgslayertreeview.h"
#include "qgslinestringv2.h"
#include "qgslogger.h"
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsmaprenderer.h"
#include "qgspolygonv2.h"
#include "qgsrubberband.h"
#include "qgsvectorlayer.h"
#include "qgsvertexmarker.h"

#include <QCursor>
#include <QPixmap>
#include <QMouseEvent>
#include <QStatusBar>


QgsMapToolCapture::QgsMapToolCapture( QgsMapCanvas* canvas, QgsAdvancedDigitizingDockWidget* cadDockWidget, CaptureMode mode )
    : QgsMapToolAdvancedDigitizing( canvas, cadDockWidget )
    , mSnappingMarker( 0 )
    , mCapturingLayer( 0 )
#ifdef Q_OS_WIN
    , mSkipNextContextMenuEvent( 0 )
#endif
{
  mCaptureMode = mode;

  // enable the snapping on mouse move / release
  mSnapOnMove = true;
  mSnapOnRelease = true;
  mSnapOnDoubleClick = false;
  mSnapOnPress = false;

  mCaptureModeFromLayer = mode == CaptureNone;

  QPixmap mySelectQPixmap = QPixmap(( const char ** ) capture_point_cursor );
  setCursor( QCursor( mySelectQPixmap, 8, 8 ) );

  connect( canvas, SIGNAL( currentLayerChanged( QgsMapLayer * ) ),
           this, SLOT( currentLayerChanged( QgsMapLayer * ) ) );
}

QgsMapToolCapture::~QgsMapToolCapture()
{
  delete mSnappingMarker;

  Q_FOREACH ( QgsVectorLayer* vlayer, settingsByLayer.keys() )
    stopCapturing( vlayer );
}

void QgsMapToolCapture::activate()
{
  startCapturing();
  QgsMapToolAdvancedDigitizing::activate();
}

void QgsMapToolCapture::deactivate()
{
  delete mSnappingMarker;
  mSnappingMarker = 0;

  QgsMapToolAdvancedDigitizing::deactivate();
}

void QgsMapToolCapture::validationFinished()
{
  emit messageDiscarded();
  QString msgFinished = tr( "Validation finished" );
  if ( !currentSettings()->mValidationWarnings.isEmpty() )
  {
    emit messageEmitted( currentSettings()->mValidationWarnings.join( "\n" ).append( "\n" ).append( msgFinished ), QgsMessageBar::WARNING );
  }
}

void QgsMapToolCapture::editingStopped()
{
  qWarning( "QgsMapToolCapture::editingStopped()" );
  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( sender() );
  stopCapturing( vlayer );
}

void QgsMapToolCapture::currentLayerChanged( QgsMapLayer *layer )
{
  if ( isCapturing() )
  {
    currentSettings()->mRubberBand->hide();
    currentSettings()->mTempRubberBand->hide();
    Q_FOREACH ( QgsVertexMarker * vertex, currentSettings()->mGeomErrorMarkers )
      vertex->hide();
  }

  if ( !mCaptureModeFromLayer )
    return;

  mCaptureMode = CaptureNone;

  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
  mCapturingLayer = settingsByLayer.contains( vlayer ) ? vlayer : 0;
  if ( !vlayer )
  {
    return;
  }

  if ( isCapturing() )
  {
    currentSettings()->mRubberBand->show();
    currentSettings()->mTempRubberBand->show();
    Q_FOREACH ( QgsVertexMarker * vertex, currentSettings()->mGeomErrorMarkers )
      vertex->show();
  }

  switch ( vlayer->geometryType() )
  {
    case QGis::Point:
      mCaptureMode = CapturePoint;
      break;
    case QGis::Line:
      mCaptureMode = CaptureLine;
      break;
    case QGis::Polygon:
      mCaptureMode = CapturePolygon;
      break;
    default:
      mCaptureMode = CaptureNone;
      break;
  }
}

void QgsMapToolCapture::cadCanvasMoveEvent( QgsMapMouseEvent * e )
{
  QgsMapToolAdvancedDigitizing::cadCanvasMoveEvent( e );
  bool snapped = e->isSnapped();
  QgsPoint point = e->mapPoint();

  if ( !snapped )
  {
    delete mSnappingMarker;
    mSnappingMarker = 0;
  }
  else
  {
    if ( !mSnappingMarker )
    {
      mSnappingMarker = new QgsVertexMarker( mCanvas );
      mSnappingMarker->setIconType( QgsVertexMarker::ICON_CROSS );
      mSnappingMarker->setColor( Qt::magenta );
      mSnappingMarker->setPenWidth( 3 );
    }
    mSnappingMarker->setCenter( point );
  }

  if ( isCapturing() && !currentSettings()->mTempRubberBand && currentSettings()->mCaptureCurve->numPoints() > 0 )
  {
    currentSettings()->mTempRubberBand = createRubberBand( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line, true );
    QgsPointV2 pt = currentSettings()->mCaptureCurve->endPoint();
    currentSettings()->mTempRubberBand->addPoint( QgsPoint( pt.x(), pt.y() ) );
    currentSettings()->mTempRubberBand->addPoint( point );
  }

  if ( isCapturing() && mCaptureMode != CapturePoint && currentSettings()->mTempRubberBand )
  {
    currentSettings()->mTempRubberBand->movePoint( point );
  }
} // mouseMoveEvent

int QgsMapToolCapture::nextPoint( const QgsPoint& mapPoint, QgsPoint& layerPoint )
{
  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( mCanvas->currentLayer() );
  if ( !vlayer )
  {
    QgsDebugMsg( "no vector layer" );
    return 1;
  }
  try
  {
    layerPoint = toLayerCoordinates( vlayer, mapPoint ); //transform snapped point back to layer crs
  }
  catch ( QgsCsException &cse )
  {
    Q_UNUSED( cse );
    QgsDebugMsg( "transformation to layer coordinate failed" );
    return 2;
  }

  return 0;
}

int QgsMapToolCapture::nextPoint( const QPoint &p, QgsPoint &layerPoint, QgsPoint &mapPoint )
{

  mapPoint = toMapCoordinates( p );
  return nextPoint( mapPoint, layerPoint );
}

int QgsMapToolCapture::addVertex( const QgsPoint& point )
{
  qWarning( "QgsMapToolCapture::addVertex()" );
  if ( mode() == CaptureNone )
  {
    QgsDebugMsg( "invalid capture mode" );
    return 2;
  }

  int res;
  QgsPoint layerPoint;
  res = nextPoint( point, layerPoint );
  if ( res != 0 )
  {
    return res;
  }

  if ( !isCapturing() )
    startCapturing();

  currentSettings()->mRubberBand->addPoint( point );
  currentSettings()->mCaptureCurve->addVertex( QgsPointV2( layerPoint.x(), layerPoint.y() ) );

  if ( !currentSettings()->mTempRubberBand )
  {
    currentSettings()->mTempRubberBand = createRubberBand( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line, true );
  }
  else
  {
    currentSettings()->mTempRubberBand->reset( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line );
  }
  if ( mCaptureMode == CaptureLine )
  {
    currentSettings()->mTempRubberBand->addPoint( point );
  }
  else if ( mCaptureMode == CapturePolygon )
  {
    const QgsPoint *firstPoint = currentSettings()->mRubberBand->getPoint( 0, 0 );
    currentSettings()->mTempRubberBand->addPoint( *firstPoint );
    currentSettings()->mTempRubberBand->movePoint( point );
    currentSettings()->mTempRubberBand->addPoint( point );
  }

  validateGeometry();

  return 0;
}

int QgsMapToolCapture::addCurve( QgsCurveV2* c )
{
  qWarning( "QgsMapToolCapture::addCurve()" );
  if ( !c )
  {
    return 1;
  }

  if ( !isCapturing() )
    startCapturing();

  if ( !currentSettings()->mRubberBand )
  {
    currentSettings()->mRubberBand = createRubberBand( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line );
  }

  QgsLineStringV2* lineString = c->curveToLine();
  QList<QgsPointV2> linePoints;
  lineString->points( linePoints );
  delete lineString;
  QList<QgsPointV2>::const_iterator ptIt = linePoints.constBegin();
  for ( ; ptIt != linePoints.constEnd(); ++ptIt )
  {
    currentSettings()->mRubberBand->addPoint( QgsPoint( ptIt->x(), ptIt->y() ) );
  }

  if ( !currentSettings()->mTempRubberBand )
  {
    currentSettings()->mTempRubberBand = createRubberBand( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line, true );
  }
  else
  {
    currentSettings()->mTempRubberBand->reset();
  }
  QgsPointV2 endPt = c->endPoint();
  currentSettings()->mTempRubberBand->addPoint( QgsPoint( endPt.x(), endPt.y() ) ); //add last point of c

  //transform back to layer CRS in case map CRS and layer CRS are different
  QgsVectorLayer* vlayer = qobject_cast<QgsVectorLayer *>( mCanvas->currentLayer() );
  const QgsCoordinateTransform* ct =  mCanvas->mapSettings().layerTransform( vlayer );
  if ( ct )
  {
    c->transform( *ct, QgsCoordinateTransform::ReverseTransform );
  }
  currentSettings()->mCaptureCurve->addCurve( c );

  return 0;
}

void QgsMapToolCapture::undo()
{
  if ( currentSettings()->mRubberBand )
  {
    int rubberBandSize = currentSettings()->mRubberBand->numberOfVertices();
    int tempRubberBandSize = currentSettings()->mTempRubberBand->numberOfVertices();
    int captureListSize = size();

    if ( rubberBandSize < 1 || captureListSize < 1 )
    {
      return;
    }

    currentSettings()->mRubberBand->removePoint( -1 );

    if ( rubberBandSize > 1 )
    {
      if ( tempRubberBandSize > 1 )
      {
        const QgsPoint *point = currentSettings()->mRubberBand->getPoint( 0, rubberBandSize - 2 );
        currentSettings()->mTempRubberBand->movePoint( tempRubberBandSize - 2, *point );
      }
    }
    else
    {
      currentSettings()->mTempRubberBand->reset( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line );
    }

    QgsVertexId vertexToRemove;
    vertexToRemove.part = 0; vertexToRemove.ring = 0; vertexToRemove.vertex = size() - 1;
    currentSettings()->mCaptureCurve->deleteVertex( vertexToRemove );

    validateGeometry();
  }
}

void QgsMapToolCapture::keyPressEvent( QKeyEvent* e )
{
  qWarning( "Key pressed" );
  if ( e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete )
  {
    undo();

    // Override default shortcut management in MapCanvas
    e->ignore();
  }
  else if ( e->key() == Qt::Key_Escape )
  {
    stopCapturing();

    // Override default shortcut management in MapCanvas
    e->ignore();
  }
}

void QgsMapToolCapture::startCapturing()
{
  if ( isCapturing() )
    return;
  qWarning( "QgsMapToolCapture::startCapturing()" );

  mCapturingLayer = currentVectorLayer();

  connect( mCapturingLayer, SIGNAL( editingStopped() ), this, SLOT( editingStopped() ), Qt::UniqueConnection );
  settingPerLayer * s = new settingPerLayer;
  s->mRubberBand = createRubberBand( mCaptureMode == CapturePolygon ? QGis::Polygon : QGis::Line );
  s->mTempRubberBand = 0;
  s->mCaptureCurve = new QgsCompoundCurveV2;
  s->mValidator = 0;
  settingsByLayer.insert( mCapturingLayer, s );
}

bool QgsMapToolCapture::isCapturing() const
{
  return mCapturingLayer != NULL;
}

void QgsMapToolCapture::stopCapturing( QgsVectorLayer *vlayer )
{
  qWarning( "QgsMapToolCapture::stopCapturing()" );
  if ( !vlayer )
    vlayer = mCapturingLayer;

  if ( !settingsByLayer.contains( vlayer ) )
    return;
  //qWarning( QString( "Stopping for layer %1" ).arg( vlayer->name() ).toUtf8().constData() );

  disconnect( vlayer, SIGNAL( editingStopped() ), this, SLOT( editingStopped() ) );

  if ( vlayer == mCapturingLayer )
    mCapturingLayer = 0;

  settingPerLayer *s = settingsByLayer[vlayer];

  delete s->mRubberBand;
  s->mRubberBand = 0;

  delete s->mTempRubberBand;
  s->mTempRubberBand = 0;

  delete s->mCaptureCurve;
  s->mCaptureCurve = 0;

  if ( s->mValidator )
    s->mValidator->deleteLater();
  s->mValidator = 0;

  while ( !s->mGeomErrorMarkers.isEmpty() )
    delete s->mGeomErrorMarkers.takeFirst();

  s->mGeomErrors.clear();

#ifdef Q_OS_WIN
  Q_FOREACH ( QWidget *w, qApp->topLevelWidgets() )
  {
    if ( w->objectName() == "QgisApp" )
    {
      if ( mSkipNextContextMenuEvent++ == 0 )
        w->installEventFilter( this );
      break;
    }
  }
#endif

  vlayer->triggerRepaint();
  settingsByLayer.remove( vlayer );
}

const QgsCompoundCurveV2* QgsMapToolCapture::captureCurve() const
{
  if ( isCapturing() )
    return currentSettings()->mCaptureCurve;
  else
  {
    qWarning( "QgsMapToolCapture::captureCurve() returns NULL");
    return 0;
  }

}

void QgsMapToolCapture::deleteTempRubberBand()
{
  if ( isCapturing() && currentSettings()->mTempRubberBand )
  {
    delete currentSettings()->mTempRubberBand;
    currentSettings()->mTempRubberBand = 0;
  }
}

void QgsMapToolCapture::closePolygon()
{
  currentSettings()->mCaptureCurve->close();
}

void QgsMapToolCapture::validateGeometry()
{
  QSettings settings;
  if ( settings.value( "/qgis/digitizing/validate_geometries", 1 ).toInt() == 0 )
    return;

  if ( currentSettings()->mValidator )
  {
    currentSettings()->mValidator->deleteLater();
    currentSettings()->mValidator = 0;
  }

  currentSettings()->mValidationWarnings.clear();
  currentSettings()->mGeomErrors.clear();
  while ( !currentSettings()->mGeomErrorMarkers.isEmpty() )
  {
    delete currentSettings()->mGeomErrorMarkers.takeFirst();
  }

  QScopedPointer<QgsGeometry> g;

  switch ( mCaptureMode )
  {
    case CaptureNone:
    case CapturePoint:
      return;
    case CaptureLine:
      if ( size() < 2 )
        return;
      g.reset( new QgsGeometry( currentSettings()->mCaptureCurve->curveToLine() ) );
      break;
    case CapturePolygon:
      if ( size() < 3 )
        return;
      QgsLineStringV2* exteriorRing = currentSettings()->mCaptureCurve->curveToLine();
      exteriorRing->close();
      QgsPolygonV2* polygon = new QgsPolygonV2();
      polygon->setExteriorRing( exteriorRing );
      g.reset( new QgsGeometry( polygon ) );
      break;
  }

  if ( !g.data() )
    return;

  currentSettings()->mValidator = new QgsGeometryValidator( g.data() );
  connect( currentSettings()->mValidator, SIGNAL( errorFound( QgsGeometry::Error ) ), this, SLOT( addError( QgsGeometry::Error ) ) );
  connect( currentSettings()->mValidator, SIGNAL( finished() ), this, SLOT( validationFinished() ) );
  currentSettings()->mValidator->start();
  messageEmitted( tr( "Validation started" ) );
}

void QgsMapToolCapture::addError( QgsGeometry::Error e )
{
  currentSettings()->mGeomErrors << e;
  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( mCanvas->currentLayer() );
  if ( !vlayer )
    return;

  currentSettings()->mValidationWarnings << e.what();

  if ( e.hasWhere() )
  {
    QgsVertexMarker *vm =  new QgsVertexMarker( mCanvas );
    vm->setCenter( mCanvas->mapSettings().layerToMapCoordinates( vlayer, e.where() ) );
    vm->setIconType( QgsVertexMarker::ICON_X );
    vm->setPenWidth( 2 );
    vm->setToolTip( e.what() );
    vm->setColor( Qt::green );
    vm->setZValue( vm->zValue() + 1 );
    currentSettings()->mGeomErrorMarkers << vm;
  }

  emit messageDiscarded();
  emit messageEmitted( currentSettings()->mValidationWarnings.join( "\n" ), QgsMessageBar::WARNING );
}

int QgsMapToolCapture::size()
{
  if ( isCapturing() )
    return currentSettings()->mCaptureCurve->numPoints();
  else
  {
    qWarning("QgsMapToolCapture::size() returning DEFAULT");
    return 0;
  }
}

QList<QgsPoint> QgsMapToolCapture::points()
{
  QList<QgsPointV2> pts;
  QList<QgsPoint> points;
  currentSettings()->mCaptureCurve->points( pts );
  QgsGeometry::convertPointList( pts, points );
  return points;
}

void QgsMapToolCapture::setPoints( const QList<QgsPoint>& pointList )
{
  if ( !isCapturing() )
    startCapturing();

  QList<QgsPointV2> pts;
  QgsGeometry::convertPointList( pointList, pts );

  QgsLineStringV2* line = new QgsLineStringV2();
  line->setPoints( pts );

  currentSettings()->mCaptureCurve->clear();
  currentSettings()->mCaptureCurve->addCurve( line );
}

QgsMapToolCapture::settingPerLayer* QgsMapToolCapture::currentSettings() const
{
  if ( !isCapturing() )
  {
    qWarning( "currentSettings() returns NULL");
    return 0;
  }
  return settingsByLayer[mCapturingLayer];
}

#ifdef Q_OS_WIN
bool QgsMapToolCapture::eventFilter( QObject *obj, QEvent *event )
{
  if ( event->type() != QEvent::ContextMenu )
    return false;

  if ( --mSkipNextContextMenuEvent == 0 )
    obj->removeEventFilter( this );

  return mSkipNextContextMenuEvent >= 0;
}
#endif

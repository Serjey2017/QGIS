/***************************************************************************
                              qgsremoteowsbuilder.cpp
                              -----------------------
  begin                : July, 2008
  copyright            : (C) 2008 by Marco Hugentobler
  email                : marco dot hugentobler at karto dot baug dot ethz dot ch
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsdatasourceuri.h"
#include "qgsremoteowsbuilder.h"
#include "qgslogger.h"
#include "qgsmslayercache.h"
#include "qgsrasterlayer.h"
#include "qgsvectorlayer.h"
#include <QDomElement>
#include <QTemporaryFile>

QgsRemoteOWSBuilder::QgsRemoteOWSBuilder( const QMap<QString, QString> &parameterMap )
  : mParameterMap( parameterMap )
{

}

QgsMapLayer *QgsRemoteOWSBuilder::createMapLayer(
  const QDomElement &elem,
  const QString &layerName,
  QList<QTemporaryFile *> &filesToRemove,
  QList<QgsMapLayer *> &layersToRemove, bool allowCaching ) const
{
  if ( elem.isNull() )
  {
    return nullptr;
  }

  //parse service element
  QDomNode serviceNode = elem.namedItem( QStringLiteral( "Service" ) );
  if ( serviceNode.isNull() )
  {
    QgsDebugMsg( "No <Service> node found, returning 0" );
    return nullptr; //service node is necessary
  }

  //parse OnlineResource element
  QDomNode onlineResourceNode = elem.namedItem( QStringLiteral( "OnlineResource" ) );
  if ( onlineResourceNode.isNull() )
  {
    QgsDebugMsg( "No <OnlineResource> element, returning 0" );
    return nullptr;
  }

  //get uri
  QDomElement onlineResourceElement = onlineResourceNode.toElement();
  QString url = onlineResourceElement.attribute( QStringLiteral( "href" ) );

  QgsMapLayer *result = nullptr;
  QString serviceName = serviceNode.toElement().text();

  //append missing ? or & at the end of the url, but only for WFS and WMS
  if ( serviceName == QLatin1String( "WFS" ) || serviceName == QLatin1String( "WMS" ) )
  {
    if ( !url.endsWith( QLatin1String( "?" ) ) && !url.endsWith( QLatin1String( "&" ) ) )
    {
      if ( url.contains( QLatin1String( "?" ) ) )
      {
        url.append( "&" );
      }
      else
      {
        url.append( "?" );
      }
    }
  }


  if ( serviceName == QLatin1String( "WFS" ) )
  {
    //support for old format where type is explicitly given and not part of url
    QString tname = onlineResourceElement.attribute( QStringLiteral( "type" ) );
    if ( !tname.isEmpty() )
    {
      url.append( "SERVICE=WFS&VERSION=1.0.0&REQUEST=GetFeature&TYPENAME=" + tname );
    }

    if ( allowCaching )
    {
      result = QgsMSLayerCache::instance()->searchLayer( url, layerName );
    }
    if ( result )
    {
      return result;
    }
    result = new QgsVectorLayer( url, layerNameFromUri( url ), QStringLiteral( "WFS" ) );
    if ( result->isValid() )
    {
      if ( allowCaching )
      {
        QgsMSLayerCache::instance()->insertLayer( url, layerName, result );
      }
      else
      {
        layersToRemove.push_back( result );
      }
    }
  }
  else if ( serviceName == QLatin1String( "WMS" ) )
  {
    result = wmsLayerFromUrl( url, layerName, layersToRemove, allowCaching );
  }
  else if ( serviceName == QLatin1String( "WCS" ) )
  {
    QgsDebugMsg( "Trying to get WCS layer" );
    result = wcsLayerFromUrl( url, layerName, filesToRemove, layersToRemove );
  }
  else if ( serviceName == QLatin1String( "SOS" ) )
  {
    result = sosLayer( elem, url, layerName, layersToRemove, allowCaching );
  }

  if ( !result || !result->isValid() )
  {
    QgsDebugMsg( "Error, maplayer is 0 or invalid" );
    if ( result )
    {
      delete result;
    }
    return nullptr;
  }

  return result;
}

QgsRasterLayer *QgsRemoteOWSBuilder::wmsLayerFromUrl( const QString &url, const QString &layerName, QList<QgsMapLayer *> &layersToRemove, bool allowCaching ) const
{
  QgsDebugMsg( "Entering" );
  QgsRasterLayer *result = nullptr;
  QString baseUrl, format, crs;
  QStringList layerList, styleList;

  if ( allowCaching )
  {
    result = qobject_cast<QgsRasterLayer *>( QgsMSLayerCache::instance()->searchLayer( url, layerName ) );
  }

  if ( result )
  {
    return result;
  }

  QStringList urlList = url.split( QStringLiteral( "?" ) );
  if ( urlList.size() < 2 )
  {
    return nullptr;
  }
  baseUrl = urlList.at( 0 );
  QStringList paramList = urlList.at( 1 ).split( QStringLiteral( "&" ) );

  QStringList::const_iterator paramIt;
  for ( paramIt = paramList.constBegin(); paramIt != paramList.constEnd(); ++paramIt )
  {
    if ( paramIt->startsWith( QLatin1String( "http" ), Qt::CaseInsensitive ) )
    {
      baseUrl = paramIt->split( QStringLiteral( "=" ) ).at( 1 );
    }
    else if ( paramIt->startsWith( QLatin1String( "FORMAT" ), Qt::CaseInsensitive ) )
    {
      format = paramIt->split( QStringLiteral( "=" ) ).at( 1 );
    }
    else if ( paramIt->startsWith( QLatin1String( "CRS" ), Qt::CaseInsensitive ) )
    {
      crs = paramIt->split( QStringLiteral( "=" ) ).at( 1 );
    }
    else if ( paramIt->startsWith( QLatin1String( "LAYERS" ), Qt::CaseInsensitive ) )
    {
      layerList = paramIt->split( QStringLiteral( "=" ) ).at( 1 ).split( QStringLiteral( "," ) );
    }
    else if ( paramIt->startsWith( QLatin1String( "STYLES" ), Qt::CaseInsensitive ) )
    {
      styleList = paramIt->split( QStringLiteral( "=" ) ).at( 1 ).split( QStringLiteral( "," ) );
    }
  }

  QgsDebugMsg( "baseUrl: " + baseUrl );
  QgsDebugMsg( "format: " + format );
  QgsDebugMsg( "crs: " + crs );
  QgsDebugMsg( "layerList first item: " + layerList.at( 0 ) );
  QgsDebugMsg( "styleList first item: " + styleList.at( 0 ) );

  QgsDataSourceUri uri;
  uri.setParam( QStringLiteral( "url" ), baseUrl );
  uri.setParam( QStringLiteral( "format" ), format );
  uri.setParam( QStringLiteral( "crs" ), crs );
  uri.setParam( QStringLiteral( "layers" ), layerList );
  uri.setParam( QStringLiteral( "styles" ), styleList );
  result = new QgsRasterLayer( uri.encodedUri(), QLatin1String( "" ), QStringLiteral( "wms" ) );
  if ( !result->isValid() )
  {
    return nullptr;
  }

  //insert into cache
  if ( allowCaching )
  {
    QgsMSLayerCache::instance()->insertLayer( url, layerName, result );
  }
  else
  {
    layersToRemove.push_back( result );
  }
  return result;
}

QgsRasterLayer *QgsRemoteOWSBuilder::wcsLayerFromUrl( const QString &url,
    const QString &layerName,
    QList<QTemporaryFile *> &filesToRemove,
    QList<QgsMapLayer *> &layersToRemove,
    bool allowCaching ) const
{
  Q_UNUSED( layerName );
  Q_UNUSED( allowCaching );
  Q_UNUSED( url )
  Q_UNUSED( filesToRemove )
  Q_UNUSED( layersToRemove )
  QgsDebugMsg( "remote http not supported with Qt5" );
  return nullptr;
}

QgsVectorLayer *QgsRemoteOWSBuilder::sosLayer( const QDomElement &remoteOWSElem, const QString &url, const QString &layerName, QList<QgsMapLayer *> &layersToRemove, bool allowCaching ) const
{
  //url for sos provider is: "url=... method=... xml=....
  QString method = remoteOWSElem.attribute( QStringLiteral( "method" ), QStringLiteral( "POST" ) ); //possible GET/POST/SOAP

  //search for <LayerSensorObservationConstraints> node that is sibling of remoteOSW element
  //parent element of <remoteOWS> (normally <UserLayer>)
  QDomElement parentElem = remoteOWSElem.parentNode().toElement();
  if ( parentElem.isNull() )
  {
    return nullptr;
  }


  //Root element of the request (can be 'GetObservation' or 'GetObservationById' at the moment, probably also 'GetFeatureOfInterest' or 'GetFeatureOfInterestTime' in the future)
  QDomElement requestRootElem;

  QDomNodeList getObservationNodeList = parentElem.elementsByTagName( QStringLiteral( "GetObservation" ) );
  if ( !getObservationNodeList.isEmpty() )
  {
    requestRootElem = getObservationNodeList.at( 0 ).toElement();
  }
  else //GetObservationById?
  {
    QDomNodeList getObservationByIdNodeList = parentElem.elementsByTagName( QStringLiteral( "GetObservationById" ) );
    if ( !getObservationByIdNodeList.isEmpty() )
    {
      requestRootElem = getObservationByIdNodeList.at( 0 ).toElement();
    }
  }

  if ( requestRootElem.isNull() )
  {
    return nullptr;
  }

  QDomDocument requestDoc;
  QDomNode newDocRoot = requestDoc.importNode( requestRootElem, true );
  requestDoc.appendChild( newDocRoot );

  QString providerUrl = "url=" + url + " method=" + method + " xml=" + requestDoc.toString();

  //check if layer is already in cache
  QgsVectorLayer *sosLayer = nullptr;
  if ( allowCaching )
  {
    sosLayer = qobject_cast<QgsVectorLayer *>( QgsMSLayerCache::instance()->searchLayer( providerUrl, layerName ) );
    if ( sosLayer )
    {
      return sosLayer;
    }
  }

  sosLayer = new QgsVectorLayer( providerUrl, QStringLiteral( "Sensor layer" ), QStringLiteral( "SOS" ) );

  if ( !sosLayer->isValid() )
  {
    delete sosLayer;
    return nullptr;
  }
  else
  {
    if ( allowCaching )
    {
      QgsMSLayerCache::instance()->insertLayer( providerUrl, layerName, sosLayer );
    }
    else
    {
      layersToRemove.push_back( sosLayer );
    }
  }
  return sosLayer;
}

/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "ElementsCollection/elementslocation.h"
#include "ElementsCollection/xmlelementcollection.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetapptest.h"
#include "qetgraphicsitem/element.h"
#include "print/printdrawing.h"
#include "qetproject.h"

#include <QDomDocument>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QtTest>

/**
	A symbol is the same size on any device: drawPicture() scales a
	picture by the ratio of the device's resolution to the default one,
	which made every symbol 96/72 too big in a PDF exported on macOS.
*/
class tst_elementpicturedpi : public QObject
{
	Q_OBJECT

		//The width of what @p element draws on an image of @p dpi
	static int drawnWidth(Element *element, int dpi)
	{
		QImage image(400, 200, QImage::Format_ARGB32);
		image.fill(Qt::white);
		image.setDotsPerMeterX(qRound(dpi / 0.0254));
		image.setDotsPerMeterY(qRound(dpi / 0.0254));
		{
			QPainter painter(&image);
			painter.translate(10, 10);
			element->paint(&painter, nullptr, nullptr);
		}
		int left = image.width(), right = -1;
		for (int y = 0 ; y < image.height() ; ++y)
			for (int x = 0 ; x < image.width() ; ++x)
				if (qGray(image.pixel(x, y)) < 128) {
					left = std::min(left, x);
					right = std::max(right, x);
				}
		return right - left;
	}

	private slots:
		void sameSizeOnAnyResolution()
		{
			QETProject project;
			Diagram *diagram = project.addNewDiagram();
			QDomDocument definition;
			definition.setContent(QStringLiteral(
				"<definition type=\"element\" link_type=\"simple\" uuid=\"%1\" "
				"width=\"120\" height=\"60\" hotspot_x=\"0\" hotspot_y=\"0\">"
				"<names><name lang=\"en\">box</name></names>"
				"<description><rect x=\"0\" y=\"0\" width=\"100\" height=\"40\" "
				"style=\"line-style:normal;line-weight:normal;filling:none;color:black\"/>"
				"</description></definition>").arg(QUuid::createUuid().toString()));
			QVERIFY(project.embeddedElementCollection()->addElementDefinition(
				QStringLiteral("import"), QStringLiteral("box"), definition.documentElement()));
			int state = 0;
			Element *element = ElementFactory::Instance()->createElement(
				ElementsLocation(QStringLiteral("embed://import/box.elmt"), &project),
				nullptr, &state);
			QVERIFY(element && !state);
			diagram->addItem(element);

			const int at72 = drawnWidth(element, 72);
			QVERIFY(qAbs(at72 - 100) <= 2);
			QCOMPARE(drawnWidth(element, 96), at72);
			QCOMPARE(drawnWidth(element, 300), at72);
		}

			//A folio printed to fit a page fills the page, not a third more
		void printedFolioFitsThePage()
		{
			QGraphicsScene scene;
			scene.addRect(0, 0, 100, 40, QPen(Qt::black, 0));
			QImage image(500, 300, QImage::Format_ARGB32);
			image.fill(Qt::white);
			image.setDotsPerMeterX(qRound(300 / 0.0254));
			image.setDotsPerMeterY(qRound(300 / 0.0254));
			{
				QPainter painter(&image);
				QetPrint::render(&scene, &painter, QRectF(0, 0, 400, 200),
								 QRectF(0, 0, 200, 100));
			}
			int right = -1;
			for (int y = 0 ; y < image.height() ; ++y)
				for (int x = 0 ; x < image.width() ; ++x)
					if (qGray(image.pixel(x, y)) < 128)
						right = std::max(right, x);
				//The 100 wide rectangle of a 200 wide folio, on 400 pixels
			QVERIFY2(qAbs(right - 200) <= 3, qPrintable(QString::number(right)));
		}
};

QET_APP_TEST_MAIN(tst_elementpicturedpi)
#include "tst_elementpicturedpi.moc"

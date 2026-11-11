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
#include "qetapptest.h"
#include "properties/terminaldata.h"

#include <QDomDocument>
#include <QGraphicsWidget>

/**
	A symbol terminal can declare a default cable type
	(potential_type) and a signal direction (signal = out | in).
*/
class tst_terminalcabledefaults : public QObject
{
	Q_OBJECT

	private:
		static QDomElement terminalElement(
			QDomDocument &doc, const QString &attributes)
		{
			const QString xml =
				QStringLiteral("<terminal x=\"1\" y=\"2\" "
					       "orientation=\"n\" %1/>")
				.arg(attributes);
			doc.setContent(xml);
			return doc.documentElement();
		}

	private slots:
		void readsAndWritesDefaults()
		{
			QDomDocument doc;
			QGraphicsWidget parent;
			TerminalData data(&parent);
			QVERIFY(data.fromXml(terminalElement(
				doc,
				"potential_type=\"SDI\" signal=\"out\"")));
			QCOMPARE(data.m_potential_type_default,
				 QStringLiteral("SDI"));
			QVERIFY(data.m_signal == TerminalData::Signal::Out);

			QDomDocument out;
			const QDomElement written = data.toXml(out);
			QCOMPARE(written.attribute("potential_type"),
				 QStringLiteral("SDI"));
			QCOMPARE(written.attribute("signal"),
				 QStringLiteral("out"));

			QDomDocument doc2;
			TerminalData in;
			QVERIFY(in.fromXml(terminalElement(
				doc2, "signal=\"in\"")));
			QVERIFY(in.m_signal == TerminalData::Signal::In);
		}

		void absentMeansNone()
		{
			QDomDocument doc;
			QGraphicsWidget parent;
			TerminalData data(&parent);
			QVERIFY(data.fromXml(terminalElement(doc, "")));
			QVERIFY(data.m_potential_type_default.isEmpty());
			QVERIFY(data.m_signal == TerminalData::Signal::None);

			QDomDocument out;
			const QDomElement written = data.toXml(out);
			QVERIFY(!written.hasAttribute("potential_type"));
			QVERIFY(!written.hasAttribute("signal"));
		}

		void unknownSignalIsNone()
		{
			QDomDocument doc;
			TerminalData data;
			QVERIFY(data.fromXml(terminalElement(
				doc, "signal=\"sideways\"")));
			QVERIFY(data.m_signal == TerminalData::Signal::None);
		}
};

QET_APP_TEST_MAIN(tst_terminalcabledefaults)
#include "tst_terminalcabledefaults.moc"

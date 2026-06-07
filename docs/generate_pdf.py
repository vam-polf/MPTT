"""
Intercom Schematic PDF Generator
E77-400MBL-01 + INMP441 + MAX98357A
"""

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle
from reportlab.lib.enums import TA_CENTER
import os

output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'intercom_schematic_final.pdf')

doc = SimpleDocTemplate(
    output_path,
    pagesize=A4,
    rightMargin=20*mm, leftMargin=20*mm,
    topMargin=20*mm, bottomMargin=20*mm
)

styles = getSampleStyleSheet()
title_style = ParagraphStyle(
    'Title',
    parent=styles['Heading1'],
    fontSize=18,
    alignment=TA_CENTER,
    spaceAfter=10,
    textColor=colors.HexColor('#1976D2')
)

heading_style = ParagraphStyle(
    'Heading',
    parent=styles['Heading2'],
    fontSize=14,
    spaceBefore=15,
    spaceAfter=10,
    textColor=colors.HexColor('#1976D2'),
    borderPadding=5,
    borderWidth=1,
    borderColor=colors.HexColor('#2196F3')
)

normal_style = ParagraphStyle(
    'Normal',
    parent=styles['Normal'],
    fontSize=9,
    leading=14,
    spaceAfter=3
)

elements = []

elements.append(Paragraph("Real-time Intercom Schematic", title_style))
elements.append(Paragraph("E77-400MBL-01 LoRa Module + INMP441 + MAX98357A", 
                          ParagraphStyle('SubTitle', parent=title_style, fontSize=12, textColor=colors.gray)))
elements.append(Spacer(1, 15*mm))

elements.append(Paragraph("1. System Architecture", heading_style))
elements.append(Paragraph("""
<b>TX Mode (PTT pressed):</b> INMP441 Mic -> I2S -> E77 Module -> LoRa TX<br/>
<b>RX Mode (PTT released):</b> E77 Module (LoRa RX) -> I2S -> MAX98357A -> Speaker
""", normal_style))
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("2. E77 Module Pin Assignment", heading_style))

pin_data = [
    ['Function', 'Pin', 'Name', 'Alt Func', 'Connection'],
    ['I2S BCLK', '30', 'PA5', 'SPI1_SCK', 'INMP441_SCK + MAX98357_BCLK'],
    ['I2S LRCK', '11', 'PB12', 'I2S2_WS', 'INMP441_WS + MAX98357_LRCK'],
    ['I2S DOUT', '14', 'PA12', 'TIM1_ETR', 'INMP441_DATA -> E77'],
    ['I2S DIN', '12', 'PA10', 'TIM1_CH3', 'E77 -> MAX98357_DIN'],
    ['PTT Key (K2)', '28', 'PA1', 'TIM2_CH2/GPIO', 'TX/RX Toggle'],
    ['UART TX', '19', 'TXD', 'Low-power UART', 'External MCU_RX'],
    ['UART RX', '20', 'RXD', 'Low-power UART', 'External MCU_TX'],
    ['3.3V', '1, 18', 'VCC', 'Power', 'All modules VDD'],
    ['GND', '6, 17', 'GND', 'Ground', 'Common ground'],
]

t = Table(pin_data, colWidths=[30*mm, 12*mm, 18*mm, 35*mm, 55*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#2196F3')),
    ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
    ('FONTSIZE', (0, 0), (-1, -1), 7),
    ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
    ('GRID', (0, 0), (-1, -1), 0.5, colors.gray),
    ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
    ('BACKGROUND', (0, 1), (-1, -1), colors.white),
    ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f5f5f5')]),
]))
elements.append(t)
elements.append(Spacer(1, 10*mm))

elements.append(Paragraph("3. Power Network (3.3V)", heading_style))
elements.append(Paragraph("""
<b>VCC (3.3V):</b> E77 Pin1/Pin18 -> INMP441 VDD -> MAX98357A VDD -> 10uF capacitor<br/>
<b>GND:</b> E77 Pin6/Pin17 -> INMP441 GND -> MAX98357A GND -> 100nF filter x3
""", normal_style))
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("4. I2S Audio Bus Connection", heading_style))

i2s_data = [
    ['Signal', 'E77 Pin', 'INMP441', 'MAX98357A'],
    ['BCLK (Bit Clock)', 'PA5 (Pin30)', 'SCK', 'BCLK'],
    ['LRCK (Word Select)', 'PB12 (Pin11)', 'WS', 'LRCK'],
    ['DATA_IN (Mic)', 'PA12 (Pin14)', 'DATA (OUT)', '-'],
    ['DATA_OUT (Amp)', 'PA10 (Pin12)', '-', 'DIN'],
]

t2 = Table(i2s_data, colWidths=[40*mm, 35*mm, 30*mm, 30*mm])
t2.setStyle(TableStyle([
    ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#1976D2')),
    ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
    ('FONTSIZE', (0, 0), (-1, -1), 9),
    ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
    ('GRID', (0, 0), (-1, -1), 0.5, colors.gray),
    ('BACKGROUND', (0, 1), (-1, -1), colors.HexColor('#E3F2FD')),
]))
elements.append(t2)
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("5. PTT Button (K2 on E77 Board)", heading_style))
key_data = [
    ['Button', 'E77 Pin', 'Function', 'Description'],
    ['K2 (PTT)', 'PA1 (Pin28)', 'TX/RX Toggle', 'Press and hold to TX, release to RX'],
]

t3 = Table(key_data, colWidths=[28*mm, 35*mm, 35*mm, 52*mm])
t3.setStyle(TableStyle([
    ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#9C27B0')),
    ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
    ('FONTSIZE', (0, 0), (-1, -1), 8),
    ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
    ('GRID', (0, 0), (-1, -1), 0.5, colors.gray),
    ('BACKGROUND', (0, 1), (-1, -1), colors.HexColor('#F3E5F5')),
]))
elements.append(t3)
elements.append(Spacer(1, 5*mm))
elements.append(Paragraph("<b>Button Circuit:</b> K2 on E77 board connected to PA1 (Pin28), using built-in pull-up", normal_style))
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("6. Module Configuration", heading_style))
elements.append(Paragraph("""
<b>INMP441:</b> L/R pin -> GND (Left channel address 0x48)<br/>
<b>MAX98357A:</b> GAIN -> GND (Default 9dB gain), SD -> 3.3V (Enable)
""", normal_style))
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("7. BOM (Bill of Materials)", heading_style))
bom_data = [
    ['Ref', 'Specification', 'Qty', 'Description'],
    ['E77', 'E77-400MBL-01', '1', 'LoRa wireless module (with K2 PTT button)'],
    ['U1', 'INMP441 module', '1', 'Digital MEMS mic'],
    ['U2', 'MAX98357A module', '1', 'I2S amplifier'],
    ['C1, C5', '10uF/10V tantalum', '2', 'Power storage'],
    ['C2-C4', '100nF SMD capacitor', '3', 'Decoupling'],
    ['SPK', '8 Ohm / 1W speaker', '1', 'Audio output'],
]

t4 = Table(bom_data, colWidths=[20*mm, 55*mm, 12*mm, 58*mm])
t4.setStyle(TableStyle([
    ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#4CAF50')),
    ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
    ('FONTSIZE', (0, 0), (-1, -1), 8),
    ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
    ('GRID', (0, 0), (-1, -1), 0.5, colors.gray),
    ('BACKGROUND', (0, 1), (-1, -1), colors.white),
    ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f5f5f5')]),
]))
elements.append(t4)
elements.append(Spacer(1, 5*mm))

elements.append(Paragraph("8. PCB Layout Guidelines", heading_style))
elements.append(Paragraph("""
<b>Routing Priority:</b><br/>
1. I2S signals (BCLK, LRCK, DATA) - Length mismatch < 5mil<br/>
2. Power traces - 3.3V (20mil), GND (30mil)<br/>
3. Button control lines - Standard (10mil)<br/><br/>
<b>Notes:</b><br/>
- Place mic near PCB edge<br/>
- Keep LoRa antenna away from audio signals<br/>
- Amplifier output away from sensitive signals
""", normal_style))

elements.append(Spacer(1, 15*mm))
elements.append(Paragraph("-" * 50, ParagraphStyle('Line', parent=title_style, fontSize=8, textColor=colors.gray)))
elements.append(Paragraph("Intercom Schematic | E77-400MBL-01 + INMP441 + MAX98357A | v3.0", 
                          ParagraphStyle('Footer', parent=title_style, fontSize=8, textColor=colors.gray)))

doc.build(elements)
print("[OK] PDF generated: " + output_path)

# Materiais, dispositivos e caixas

## Arquitetura
```
 [Sonda JSN-SR04T]──cabo──[CAIXA A: Controle (ESP32)]──Wi-Fi──┬──[CAIXA C: Relé Wi-Fi]──►Bomba
   na tampa da caixa d'água   perto da caixa d'água           ├──[CAIXA B: Display (opcional, 2º ESP32)]
                                                               └──PC (IHM no navegador)
```
O ESP32 da Caixa A decide tudo (liga/desliga, alarmes) e oferece a API HTTP. O PC e o display só
mostram e comandam. Se o PC ou o display saírem do ar, a automação continua funcionando.

**Display local:** `USAR_TFT_LOCAL = 1` no firmware liga o TFT direto no ESP32 da Caixa A (uma unidade só).
Se o display ficar longe do sensor, use um segundo ESP32 (placa CYD) só para exibir os dados da API
(ainda não implementado; SPI não deve passar de ~30 cm de cabo).

## 1. Lista de materiais

Faixas de preço são estimativas aproximadas e variam muito. Confira nos anúncios.

### Caixa A: controle do sensor
| Qtd | Item | Observação |
|---|---|---|
| 1 | ESP32 DevKit V1 (30 pinos) | Com base de terminais parafusáveis, se possível |
| 1 | Sensor JSN-SR04T / AJ-SR04M com sonda | O do anúncio (~R$ 50) |
| 1 | Resistor 1 kΩ + 1 resistor 2 kΩ | Divisor de tensão do ECHO (5V → 3,3V) |
| 1 | Capacitor 470 µF / 10 V | Filtro na alimentação do sensor |
| 1 | Fonte 5V 2A (ou módulo HLK-5M05) | Tomada próxima, ou fonte USB com cabo |
| 2 | Prensa-cabos PG7/PG9 | Entrada do cabo do sensor e da alimentação |
| 1 | Sachê de sílica gel | Contra umidade dentro da caixa |
| 1 | Cabo blindado 4 vias 22 AWG | Só se precisar estender o cabo da sonda (máx. ~5 m) |
| 1 | Kit fita auto-fusão, abraçadeiras, silicone neutro | Vedação e fixação |

### Caixa B: display (opcional)
| Qtd | Item | Observação |
|---|---|---|
| 1 | Display TFT ILI9341 2.8" SPI (se ligado na Caixa A) **ou** placa CYD ESP32-2432S028 (se remoto) | A CYD já traz ESP32 + tela de 2.8" |
| 1 | Cabo USB + fonte 5V 1A | Para a CYD remota |

### Caixa C: relé Wi-Fi e potência
| Qtd | Item | Observação |
|---|---|---|
| 1 | **Shelly 1** (Gen1) ou **Shelly Plus 1** (Gen2) | Defina `RELE_TIPO` no firmware. Alternativa: Sonoff Basic + Tasmota |
| 1 | Disjuntor bipolar 10-16 A (ou 1P) + DR | Proteção da bomba |
| 1 | Contator 220V (ex.: 9 A) | **Só se a bomba for maior que ½ cv.** O Shelly aciona a bobina |
| 1 | DPS (opcional) | Proteção contra surto |
| 1 | Borneiras / terminais | Ligações |
| 1 | Boia elétrica mecânica | Em série com a bomba: proteção independente contra transbordamento |

### Ferramentas e consumíveis
Multímetro, ferro de solda e solda, alicate de crimpar, chaves, fio flexível 1,5 mm² (potência) e 0,5 mm² (sinal).

## 2. Caixa A: placa de controle do sensor
- **Tipo:** caixa de sobrepor **IP65**, ABS ou policarbonato, aprox. **120 × 120 × 60 mm** (altura interna ≥ 55 mm
  para o ESP32 de 30 pinos com base de terminais).
- **Onde:** na parede ou estrutura **próxima à caixa d'água**, protegida de sol direto e chuva, a menos de 2,5 m da sonda
  (o cabo original tem ~2,5 m).
- **Como:** placa fixa por espaçadores; prensa-cabos voltados para baixo (evita pingos); sílica gel dentro;
  alça de gotejamento no cabo (curva para baixo antes de entrar na caixa).
- **Cuidados:** não feche com Wi-Fi bloqueado por metal. Use caixa plástica, não metálica. Deixe a antena do ESP32
  longe de partes metálicas.

## 3. Caixa B: display
- **Tipo:** gabinete para display 2.8" com **janela frontal**, aprox. **100 × 65 × 25 mm**. A CYD
  (86 × 50 mm de placa) tem vários gabinetes prontos para impressão 3D. Ou caixa ABS de sobrepor com
  recorte para a tela.
- **Onde:** dentro de casa, na altura dos olhos, perto de uma tomada USB (cozinha, área de serviço).
- **Cuidados:** furo para o cabo USB e para o botão de reset; evitar sol direto na tela.
- Se o display for **ligado ao ESP32 da Caixa A**, use um gabinete de painel com cabo curto (≤ 30 cm) ou monte a tela
  na própria tampa da Caixa A (tampa transparente).

## 4. Caixa C: relé Wi-Fi
- **Dispositivo pequeno (Shelly 1: 41 × 36 × 17 mm), sem contator:** caixa de sobrepor **IP54/IP65 ~ 100 × 100 × 55 mm**
  com borneira, perto da bomba ou do quadro.
- **Com contator + disjuntor:** quadro de distribuição de sobrepor **IP65**, **4 a 8 módulos DIN**,
  com trilho DIN. Disjuntor + DR + contator + Shelly.
- **Onde:** próximo à bomba ou ao quadro elétrico, com Wi-Fi com sinal forte (confira o RSSI no IHM, ideal > -70 dBm).
- **Cuidados:** parte de 110/220V só em caixa fechada, com aterramento. Esta caixa é a que exige
  eletricista habilitado se você não tem experiência.
- Configure **IP fixo** (reserva DHCP no roteador) e o **auto-off** do relé (ex.: 3600 s) como proteção extra.

## 5. Ordem de montagem sugerida
1. Bancada: ESP32 + sensor + divisor + TFT, com firmware e IHM em modo demo e depois real.
2. Calibre o sensor na caixa d'água (vazia/cheia) pela tela **Configuração** da IHM.
3. Teste o relé com uma lâmpada.
4. Instale as caixas, ligue a bomba por último e acompanhe um ciclo completo.

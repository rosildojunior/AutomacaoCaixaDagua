# Automação da Caixa d'Água

Monitora o nível da caixa d'água com um sensor ultrassônico, mostra em um display TFT
e liga/desliga a bomba por um relé Wi-Fi (histerese: liga em nível baixo, desliga ao encher).

> Lista completa de materiais e caixas: [docs/MATERIAIS_E_CAIXAS.md](docs/MATERIAIS_E_CAIXAS.md)

## IHM no PC
Abra `ihm/index.html` no navegador, informe o IP do ESP32 e clique em *Conectar* (ou marque *Modo demo*).
Telas: Painel (nível, tendência), Controle (auto/manual, bomba), Configuração (níveis e calibração) e
Alarmes e falhas (ativos + histórico). O firmware expõe a API em `/api/status`, `/api/config`, `/api/mode`,
`/api/pump` e `/api/ack`.

## Materiais (resumo)
| Item | Observação |
|---|---|
| ESP32 DevKit | Wi-Fi integrado |
| Display TFT ILI9341 2.4"/2.8" SPI | Alternativa: placa CYD ESP32-2432S028 (ESP32 + tela) |
| Sensor ultrassônico à prova d'água JSN-SR04T / AJ-SR04M | Alimentado com 5V |
| Resistores 1 kΩ e 2 kΩ | Divisor de tensão no ECHO |
| Relé Wi-Fi Shelly 1 ou Sonoff Basic (Tasmota) | API HTTP local. Relé RF 433 MHz **não serve** |
| Fonte 5V 1A e caixa estanque | |

## Sensor
- Alcance 20 cm a 8 m. **Zona cega de 20 cm**: instale a sonda a no mínimo 25 cm acima do nível máximo.
- Ângulo de ~15°: afaste de paredes e boias. Condensação pode gerar leituras falsas (o código usa mediana).

## Ligações
```
Sensor 5V   -> VIN (5V) do ESP32      Sensor GND -> GND
Sensor TRIG -> GPIO 5
Sensor ECHO -> divisor (1k em série + 2k para GND) -> GPIO 18
```
O ECHO sai em 5V; o ESP32 aguenta 3,3V. **Sem o divisor o pino pode queimar.**

TFT: configure o `User_Setup.h` da biblioteca TFT_eSPI. O SCLK padrão (GPIO 18) conflita com o ECHO:
mude o SCLK para o GPIO 14. Na placa CYD isso já vem configurado.

## Lógica
(O firmware v2 também tem alarme de relé sem resposta e de nível crítico, e trava a bomba após o tempo máximo até o reconhecimento na IHM.)

- Nível % = (DIST_VAZIA − distância) / (DIST_VAZIA − DIST_CHEIA).
- Liga a bomba com nível ≤ 25%; desliga com ≥ 98%. Entre os dois, mantém o estado.
- Proteções: tempo máximo de 60 min de bomba ligada; sem leitura válida do sensor, desliga a bomba e mostra ERRO.
- Configure também o **auto-off** no próprio Shelly/Sonoff (ex.: 3600 s), caso o ESP32 trave com a bomba ligada.
- Atenção: se o tempo máximo estourar com o nível ainda baixo, o código liga de novo no ciclo seguinte.

## Uso
1. Instale a biblioteca `TFT_eSPI` na Arduino IDE (placa ESP32).
2. Edite Wi-Fi, IP do relé e `TASMOTA` no início de `firmware/caixa_dagua/caixa_dagua.ino`.
3. Calibre: com a caixa vazia leia a distância (acrescente `Serial.println(d)`) e ajuste `DIST_VAZIA_CM`;
   com a caixa cheia, ajuste `DIST_CHEIA_CM`.
4. Teste a bomba primeiro com uma lâmpada no relé.

## Segurança elétrica
- Relé de no mínimo 10 A. Bomba acima de ½ cv: use um contator acionado pelo relé Wi-Fi.
- Parte 110/220V em caixa fechada, com disjuntor/DR e aterramento. Chame um eletricista se necessário.
- Recomendado: boia mecânica em série como proteção extra contra transbordamento.

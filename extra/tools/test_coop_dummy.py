#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Script para testar o Hunt Bell sozinho, simulando um co-op bot no servidor."""

import argparse
import base64
import json
import time
import urllib.request

DEFAULT_SERVER = "http://play.seamlesshunt.com:31315"


def main():
    parser = argparse.ArgumentParser(description="Simulador de Convidado (Kin Bell)")
    parser.add_argument("--server", default=DEFAULT_SERVER, help="URL WebAPI do servidor")
    parser.add_argument("--password", "-p", default="", help="Senha (ex: cakey)")
    parser.add_argument("--area", type=int, default=241010, help="ID da Área (padrão: 241010 Central Yharnam)")
    parser.add_argument("--level", type=int, default=16, help="Nível do personagem simulado")
    args = parser.parse_args()

    session_id = "bot-test-0000-0000-0000-000000000001"
    user_id = 888123

    # Monta SummonData com o nome "HunterBot"
    name_bytes = b"HunterBot\x00\x00\x00\x00\x00\x00\x00"
    raw_data = bytearray(224)
    raw_data[0x40:0x50] = name_bytes
    raw_data[0x70:0x74] = args.level.to_bytes(4, "little")
    raw_b64 = base64.b64encode(raw_data).decode("ascii")

    create_payload = {
        "AreaId": 402718720,
        "AreaRegionId": args.area,
        "ChannelId": 0,
        "CharaId": 9223372036854775808,
        "ClientVersion": 0,
        "MatchingLevel": args.level,
        "MessageId": "SummonDataCreateRequest",
        "NatType": 0,
        "NaturalEnemy_BloodHunter": 0,
        "NaturalEnemy_HunterOfHunter": 0,
        "NaturalEnemy_Vileblood": 0,
        "NaturalEnemy_VilebloodHunter": 0,
        "PosX": -194,
        "PosY": -28,
        "PosZ": 67,
        "Region": 2,
        "RegionFlag": 1,
        "SessionId": session_id,
        "SummonData": raw_b64,
        "SummonDataVersion": 3,
        "SummonMethod": 0,
        "SummonType": 0,
        "SummonWord": args.password if args.password else None,
        "UnlockedFlagList": [{"UnlockedFlag": 0}, {"UnlockedFlag": 0}, {"UnlockedFlag": 0}],
        "UserId": user_id,
    }

    url_create = args.server.rstrip("/") + "/summon_messenger/create"
    url_delete = args.server.rstrip("/") + "/summon_messenger/delete"
    url_get = args.server.rstrip("/") + "/summon_messenger/get"

    print("=" * 60)
    print("🤖 SIMULADOR DE CONVIDADO (KIN BELL) PARA TESTE SOLO")
    print(f"   Servidor: {args.server}")
    print(f"   Personagem: HunterBot (Nv {args.level})")
    print(f"   Área: {args.area} (Central Yharnam)")
    print(f"   Senha: '{args.password}'" if args.password else "   Senha: [PÚBLICA / SEM SENHA]")
    print("=" * 60)

    # 1. Envia anúncio para o servidor
    req = urllib.request.Request(
        url_create, data=json.dumps(create_payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}, method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode())
        if data.get("ResKind") != 0:
            print(f"❌ Erro ao criar marca: {data}")
            return
        print("✅ Marca de 'HunterBot' publicada com sucesso no servidor!")
        print("👉 Agora entre no jogo com seu personagem e toque o HUNT BELL!")
        print("   (Aguardando o seu jogo encontrar a marca... Pressione Ctrl+C para encerrar)\n")
    except Exception as exc:
        print(f"❌ Falha de rede: {exc}")
        return

    # 2. Fica monitorando se a marca foi reivindicada pelo jogo do jogador
    try:
        start_t = time.time()
        while time.time() - start_t < 120:
            time.sleep(3)
            # Checa se a marca ainda está lá
            q_body = json.dumps({"SummonWord": args.password}).encode("utf-8") if args.password else b"{}"
            q_req = urllib.request.Request(
                url_get, data=q_body, headers={"Content-Type": "application/json"}, method="POST"
            )
            with urllib.request.urlopen(q_req, timeout=5) as q_resp:
                q_data = json.loads(q_resp.read().decode())
            found = False
            for s in q_data.get("SummonDataList", []):
                if s.get("UserId") == user_id:
                    found = True
                    break

            if not found:
                print("🎉🎉🎉 O SEU JOGO ENCONTROU A MARCA E REIVINDICOU O HUNTERBOT! 🎉🎉🎉")
                print("O Hunt Bell está 100% funcionando! O seu jogo puxou a conexão do servidor!")
                break
            else:
                elapsed = int(time.time() - start_t)
                print(f"⏳ [{elapsed}s] HunterBot ainda aguardando a badalada do seu Hunt Bell...")
    except KeyboardInterrupt:
        print("\nCancelando...")
    finally:
        del_payload = {"MessageId": "SummonDataRemoveRequest", "SessionId": session_id, "UserId": user_id, "Force": True}
        del_req = urllib.request.Request(url_delete, data=json.dumps(del_payload).encode("utf-8"),
                                         headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(del_req, timeout=5):
                pass
            print("🧹 Marca de teste removida do servidor.")
        except Exception:
            pass


if __name__ == "__main__":
    main()

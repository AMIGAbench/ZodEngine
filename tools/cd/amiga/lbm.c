/* ILBM/PBM-Leser. Format und Begruendung: lbm.h */

#include "lbm.h"

#include <string.h>

#ifdef __amigaos__
#include <proto/exec.h>
#endif

/* IFF ist BIG-Endian. */
static ULONG be32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16)
	     | ((ULONG)p[2] << 8)  |  (ULONG)p[3];
}

static UWORD be16(const UBYTE *p)
{
	return (UWORD)(((UWORD)p[0] << 8) | p[1]);
}

/* ByteRun1 auspacken. Gibt die Zahl der geschriebenen Byte zurueck; bei
 * einem Ueberlauf des Ziels wird abgebrochen (und die Zahl passt dann nicht
 * zum Erwarteten, was der Aufrufer prueft). */
static ULONG byterun1(const UBYTE *q, ULONG qlen, UBYTE *z, ULONG zlen)
{
	ULONG i = 0, o = 0;

	while(i < qlen && o < zlen)
	{
		UBYTE n = q[i++];

		if(n < 128)
		{
			ULONG k = (ULONG)n + 1;

			if(i + k > qlen) k = qlen - i;
			if(o + k > zlen) k = zlen - o;

			memcpy(z + o, q + i, k);

			i += k;
			o += k;
		}
		else if(n > 128)
		{
			ULONG k = 257u - (ULONG)n;
			UBYTE c;

			if(i >= qlen) break;

			c = q[i++];

			if(o + k > zlen) k = zlen - o;

			memset(z + o, c, k);

			o += k;
		}
		/* n == 128: Fuellung, nichts zu tun */
	}

	return o;
}

BOOL lbm_lesen(const UBYTE *daten, ULONG len, struct LbmBild *aus,
               UBYTE *pixel, ULONG platz)
{
	ULONG i;
	int ist_pbm = 0;
	int haben_bmhd = 0;
	UBYTE packung = 0;
	const UBYTE *body = NULL;
	ULONG body_len = 0;

	if(!daten || !aus || !pixel) return FALSE;
	if(len < 12) return FALSE;
	if(memcmp(daten, "FORM", 4) != 0) return FALSE;

	if(memcmp(daten + 8, "PBM ", 4) == 0)       ist_pbm = 1;
	else if(memcmp(daten + 8, "ILBM", 4) == 0)  ist_pbm = 0;
	else return FALSE;

	memset(aus, 0, sizeof(*aus));

	/* Bloecke ab 12 durchlaufen. Jeder hat vier Kennbyte, u32 Laenge und
	 * danach die Nutzlast; ungerade Laengen werden mit einem Fuellbyte auf
	 * gerade gebracht -- das steht NICHT in der Laenge. */
	i = 12;

	while(i + 8 <= len)
	{
		const UBYTE *kid = daten + i;
		ULONG kl = be32(daten + i + 4);
		const UBYTE *nutz = daten + i + 8;

		if(i + 8 + kl > len) break;

		if(memcmp(kid, "BMHD", 4) == 0 && kl >= 20)
		{
			aus->w      = be16(nutz + 0);
			aus->h      = be16(nutz + 2);
			aus->ebenen = nutz[8];
			packung     = nutz[10];
			haben_bmhd  = 1;
		}
		else if(memcmp(kid, "CMAP", 4) == 0)
		{
			ULONG n = kl / 3;
			ULONG k;
			UBYTE hoechst = 0;

			if(n > 256) n = 256;

			for(k = 0; k < n * 3; k++)
				if(nutz[k] > hoechst) hoechst = nutz[k];

			/* CMAP kann 6-Bit-VGA-Werte tragen (0..63) oder volle
			 * 8 Bit. Bleibt alles unter 64, ist es VGA und muss
			 * hochgezogen werden -- sonst waere das ganze Bild
			 * viermal zu dunkel. Die Bilder aus main.pac tragen
			 * volle 8 Bit; DOOR.PAC ebenso. Die Pruefung steht
			 * trotzdem hier, weil sie billig ist und ein zu
			 * dunkles Bild sonst wie ein Palettenfehler aussaehe. */
			for(k = 0; k < n * 3; k++)
				aus->palette[k] = hoechst < 64
				                ? (UBYTE)(nutz[k] * 4)
				                : nutz[k];

			aus->farben = (UWORD)n;
		}
		else if(memcmp(kid, "BODY", 4) == 0)
		{
			body = nutz;
			body_len = kl;
		}

		i += 8 + kl + (kl & 1);
	}

	if(!haben_bmhd || !body) return FALSE;
	if(aus->w == 0 || aus->h == 0) return FALSE;
	if((ULONG)aus->w * aus->h > platz) return FALSE;

	aus->pixel = pixel;

	if(ist_pbm)
	{
		ULONG soll = (ULONG)aus->w * aus->h;

		if(packung == 1)
		{
			if(byterun1(body, body_len, pixel, soll) != soll)
				return FALSE;
		}
		else
		{
			if(body_len < soll) return FALSE;

			memcpy(pixel, body, soll);
		}

		return TRUE;
	}

	/* ILBM: planar. Je Zeile stehen erst alle Bytes der Ebene 0, dann die
	 * der Ebene 1 und so fort. Die Zeilenlaenge EINER Ebene ist auf ein
	 * gerades Byte aufgerundet (siehe lbm.h). */
	{
		ULONG bpr = (((ULONG)aus->w + 15u) / 16u) * 2u;
		ULONG zeile_len = bpr * aus->ebenen;
		ULONG soll = zeile_len * aus->h;
		UBYTE *plan;
		UBYTE *frei = NULL;
		ULONG y;

		if(aus->ebenen == 0 || aus->ebenen > 8) return FALSE;

		if(packung == 1)
		{
			/* Ausgepackt brauchen wir den ganzen planaren Block.
			 * Er wird gleich in `pixel` umgesetzt, passt dort aber
			 * nicht hinein (planar ist groesser als chunky, sobald
			 * ebenen > 8 -- und gleich gross bei genau 8). Deshalb
			 * ein eigener Puffer. */
			frei = (UBYTE *)AllocVec(soll, MEMF_ANY);

			if(!frei) return FALSE;

			if(byterun1(body, body_len, frei, soll) != soll)
			{
				FreeVec(frei);

				return FALSE;
			}

			plan = frei;
		}
		else
		{
			if(body_len < soll) return FALSE;

			plan = (UBYTE *)body;
		}

		for(y = 0; y < aus->h; y++)
		{
			const UBYTE *zq = plan + y * zeile_len;
			UBYTE *zz = pixel + y * aus->w;
			ULONG x;

			/* Erst nullen, dann Ebene fuer Ebene die Bits
			 * hineinodern. Andersherum (je Punkt alle Ebenen
			 * lesen) waere derselbe Aufwand, springt aber je
			 * Bildpunkt achtmal durch den Speicher. */
			memset(zz, 0, aus->w);

			{
				ULONG p;

				for(p = 0; p < aus->ebenen; p++)
				{
					const UBYTE *eb = zq + p * bpr;
					UBYTE bit = (UBYTE)(1u << p);

					for(x = 0; x < aus->w; x++)
						if(eb[x >> 3] & (0x80u >> (x & 7)))
							zz[x] |= bit;
				}
			}
		}

		if(frei) FreeVec(frei);
	}

	return TRUE;
}

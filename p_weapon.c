// g_weapon.c

#include "g_local.h"
#include "m_player.h"


static qboolean	is_quad;
static byte		is_silenced;


void weapon_grenade_fire (edict_t *ent, qboolean held);


static void P_ProjectSource (gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance;

	VectorCopy (distance, _distance);
	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource (point, _distance, forward, right, result);
}


/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon
target noise (bullet wall impacts)

Monsters that don't directly see the player can move
to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t *who, vec3_t where, int type)
{
	edict_t		*noise;

	if (type == PNOISE_WEAPON)
	{
		if (who->client->silencer_shots)
		{
			who->client->silencer_shots--;
			return;
		}
	}

	if (deathmatch->value)
		return;

	if (who->flags & FL_NOTARGET)
		return;


	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		level.sound_entity = noise;
		level.sound_entity_framenum = level.framenum;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		level.sound2_entity = noise;
		level.sound2_entity_framenum = level.framenum;
	}

	VectorCopy (where, noise->s.origin);
	VectorSubtract (where, noise->maxs, noise->absmin);
	VectorAdd (where, noise->maxs, noise->absmax);
	noise->teleport_time = level.time;
	gi.linkentity (noise);
}


qboolean Pickup_Weapon (edict_t *ent, edict_t *other)
{
	int			index;
	gitem_t		*ammo;

	index = ITEM_INDEX(ent->item);

	if ( ( ((int)(dmflags->value) & DF_WEAPONS_STAY) || coop->value) 
		&& other->client->pers.inventory[index])
	{
		if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM) ) )
			return false;	// leave the weapon for others to pickup
	}

	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & DROPPED_ITEM) )
	{
		// give them some ammo with it
		ammo = FindItem (ent->item->ammo);
		if ( (int)dmflags->value & DF_INFINITE_AMMO )
			Add_Ammo (other, ammo, 1000);
		else
			Add_Ammo (other, ammo, ammo->quantity);

		if (! (ent->spawnflags & DROPPED_PLAYER_ITEM) )
		{
			if (deathmatch->value)
			{
				if ((int)(dmflags->value) & DF_WEAPONS_STAY)
					ent->flags |= FL_RESPAWN;
				else
					SetRespawn (ent, 30);
			}
			if (coop->value)
				ent->flags |= FL_RESPAWN;
		}
	}

	if (other->client->pers.weapon != ent->item && 
		(other->client->pers.inventory[index] == 1) &&
		( !deathmatch->value || other->client->pers.weapon == FindItem("blaster") ) )
		other->client->newweapon = ent->item;

	return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon (edict_t *ent)
{
	int i;

	if (ent->client->grenade_time)
	{
		ent->client->grenade_time = level.time;
		ent->client->weapon_sound = 0;
		weapon_grenade_fire (ent, false);
		ent->client->grenade_time = 0;
	}

	ent->client->pers.lastweapon = ent->client->pers.weapon;
	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = NULL;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == 255) {
		if (ent->client->pers.weapon)
			i = ((ent->client->pers.weapon->weapmodel & 0xff) << 8);
		else
			i = 0;
		ent->s.skinnum = (ent - g_edicts - 1) | i;
	}

	if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
		ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->pers.weapon->ammo));
	else
		ent->client->ammo_index = 0;

	if (!ent->client->pers.weapon)
	{	// dead
		ent->client->ps.gunindex = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;
	ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);

	ent->client->anim_priority = ANIM_PAIN;
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
			ent->s.frame = FRAME_crpain1;
			ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
			ent->s.frame = FRAME_pain301;
			ent->client->anim_end = FRAME_pain304;
			
	}
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange (edict_t *ent)
{
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("slugs"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("railgun"))] )
	{
		ent->client->newweapon = FindItem ("railgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("cells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("hyperblaster"))] )
	{
		ent->client->newweapon = FindItem ("hyperblaster");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("chaingun"))] )
	{
		ent->client->newweapon = FindItem ("chaingun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("machinegun"))] )
	{
		ent->client->newweapon = FindItem ("machinegun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))] > 1
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("super shotgun"))] )
	{
		ent->client->newweapon = FindItem ("super shotgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("shotgun"))] )
	{
		ent->client->newweapon = FindItem ("shotgun");
		return;
	}
	//ent->client->newweapon = FindItem ("blaster"); johnnyb
		ent->client->newweapon = FindItem ("Hands");
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon (edict_t *ent)
{
	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = NULL;
		ChangeWeapon (ent);
	}

	// call active weapon think routine
	if (ent->client->pers.weapon && ent->client->pers.weapon->weaponthink)
	{
		is_quad = (ent->client->quad_framenum > level.framenum);
		if (ent->client->silencer_shots)
			is_silenced = MZ_SILENCED;
		else
			is_silenced = 0;
		ent->client->pers.weapon->weaponthink (ent);
	}
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon (edict_t *ent, gitem_t *item)
{
	int			ammo_index;
	gitem_t		*ammo_item;

	//ent->client->ClassSpeed = 5;
	// see if we're already using it
	if (item == ent->client->pers.weapon)
		return;

	if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO))
	{
		ammo_item = FindItem(item->ammo);
		ammo_index = ITEM_INDEX(ammo_item);

		if (!ent->client->pers.inventory[ammo_index])
		{
			gi.cprintf (ent, PRINT_HIGH, "No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}

		if (ent->client->pers.inventory[ammo_index] < item->quantity)
		{
			gi.cprintf (ent, PRINT_HIGH, "Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}
	}

	// change to this weapon when down
	ent->client->newweapon = item;
}



/*
================
Drop_Weapon
================
*/
void Drop_Weapon (edict_t *ent, gitem_t *item)
{
	int		index;

	if ((int)(dmflags->value) & DF_WEAPONS_STAY)
		return;

	index = ITEM_INDEX(item);
	// see if we're already using it
	if ( ((item == ent->client->pers.weapon) || (item == ent->client->newweapon))&& (ent->client->pers.inventory[index] == 1) )
	{
		gi.cprintf (ent, PRINT_HIGH, "Can't drop current weapon\n");
		return;
	}

	Drop_Item (ent, item);
	ent->client->pers.inventory[index]--;
}


/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST		(FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST		(FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST	(FRAME_IDLE_LAST + 1)

void Weapon_Generic (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent))
{
	int		n;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
		{
			ChangeWeapon (ent);
			return;
		}
		else if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;

		if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if ((!ent->client->ammo_index) || 
				( ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity))
			{
				ent->client->ps.gunframe = FRAME_FIRE_FIRST;
				ent->client->weaponstate = WEAPON_FIRING;

				// start the animation
				ent->client->anim_priority = ANIM_ATTACK;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crattak1-1;
					ent->client->anim_end = FRAME_crattak9;
				}
				else
				{
					ent->s.frame = FRAME_attack1-1;
					ent->client->anim_end = FRAME_attack8;
				}
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
		}
		else
		{
			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (n = 0; pause_frames[n]; n++)
				{
					if (ent->client->ps.gunframe == pause_frames[n])
					{
						if (rand()&15)
							return;
					}
				}
			}

			ent->client->ps.gunframe++;
			return;
		}
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		for (n = 0; fire_frames[n]; n++)
		{
			if (ent->client->ps.gunframe == fire_frames[n])
			{
				if (ent->client->quad_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

				fire (ent);
				break;
			}
		}

		if (!fire_frames[n])
			ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST+1)
			ent->client->weaponstate = WEAPON_READY;
	}
}


/*
======================================================================

GRENADE

======================================================================
*/

#define GRENADE_TIMER		3.0 //normally 3.0
#define GRENADE_MINSPEED	400 //400
#define GRENADE_MAXSPEED	800 //800

void weapon_grenade_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = 1; //normally 125
	float	timer;
	int		speed;
	float	radius;

	radius = 1000;
	if (is_quad)
		damage *= 4;
	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	timer = ent->client->grenade_time - level.time;
	speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
	fire_grenade2 (ent, start, forward, damage, speed, timer, radius, held);

	//if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		//ent->client->pers.inventory[ent->client->ammo_index]--;
	//if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 3) // requires 10 cells
	//{
		//gi.cprintf (ent, PRINT_HIGH, "You need 3 mana to use Grenade Distraction\n"); // Notify them
		//return; // Stop the command from going
	//}
    //ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 3;
	ent->client->grenade_time = level.time + 1.0;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->health <= 0)
		return;

	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->anim_priority = ANIM_ATTACK;
		ent->s.frame = FRAME_crattak1-1;
		ent->client->anim_end = FRAME_crattak3;
	}
	else
	{
		ent->client->anim_priority = ANIM_REVERSE;
		ent->s.frame = FRAME_wave08;
		ent->client->anim_end = FRAME_wave01;
	}
}

void Weapon_Grenade (edict_t *ent)
{
	if ((ent->client->newweapon) && (ent->client->weaponstate == WEAPON_READY))
	{
		ChangeWeapon (ent);
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if (ent->client->pers.inventory[ent->client->ammo_index])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0;
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
			return;
		}

		if ((ent->client->ps.gunframe == 29) || (ent->client->ps.gunframe == 34) || (ent->client->ps.gunframe == 39) || (ent->client->ps.gunframe == 48))
		{
			if (rand()&15)
				return;
		}

		if (++ent->client->ps.gunframe > 48)
			ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		if (ent->client->ps.gunframe == 5)
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

		if (ent->client->ps.gunframe == 11)
		{
			if (!ent->client->grenade_time)
			{
				ent->client->grenade_time = level.time + GRENADE_TIMER + 0.2;
				ent->client->weapon_sound = gi.soundindex("weapons/hgrenc1b.wav");
			}

			// they waited too long, detonate it in their hand
			if (!ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
			{
				ent->client->weapon_sound = 0;
				weapon_grenade_fire (ent, true);
				ent->client->grenade_blew_up = true;
			}

			if (ent->client->buttons & BUTTON_ATTACK)
				return;

			if (ent->client->grenade_blew_up)
			{
				if (level.time >= ent->client->grenade_time)
				{
					ent->client->ps.gunframe = 15;
					ent->client->grenade_blew_up = false;
				}
				else
				{
					return;
				}
			}
		}

		if (ent->client->ps.gunframe == 12)
		{
			ent->client->weapon_sound = 0;
			weapon_grenade_fire (ent, false);
		}

		if ((ent->client->ps.gunframe == 15) && (level.time < ent->client->grenade_time))
			return;

		ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == 16)
		{
			ent->client->grenade_time = 0;
			ent->client->weaponstate = WEAPON_READY;
		}
	}
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire (edict_t *ent)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = 120;
	float	radius;

	radius = damage+40;
	if (is_quad)
		damage *= 4;
	
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 10) // requires 10 cells
		{
			gi.cprintf (ent, PRINT_HIGH, "You need 20 mana to use Suck\n"); // Notify them
			//ent->client->ps.gunframe = 32;
			//ent->client->ps.gunframe++;
			ent->client->ps.gunframe++;
			return; // Stop the command from going
		}
	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	//fire_grenade (ent, start, forward, damage, 600, 2.5, radius);
	fire_grenade (ent, start, forward, damage, 600, 13, radius);
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_GRENADE | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	//PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
				ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 10;
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
}

void Weapon_GrenadeLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	vec3_t mine; //johnnyb
	int		damage;
	float	damage_radius;
	int		radius_damage;

	damage = 100 + (int)(random() * 20.0);
	radius_damage = 120;
	damage_radius = 120;
	if (is_quad)
	{
		damage *= 4;
		radius_damage *= 4;
	}
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 20) // requires 10 cells
		{
			gi.cprintf (ent, PRINT_HIGH, "You need 20 mana to use Suck\n"); // Notify them
			//ent->client->ps.gunframe = 32;
			//ent->client->ps.gunframe++;
			ent->client->ps.gunframe++;
			return; // Stop the command from going
		}
	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	forward[0] = forward[0] - 9;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage); //normally 650
	forward[0] = forward[0] + 3;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage);
	forward[0] = forward[0] + 3;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage);
	forward[0] = forward[0] + 3;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage);
	forward[0] = forward[0] + 3;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage);
	forward[0] = forward[0] + 3;
	fire_rocket (ent, start, forward, damage, 200, damage_radius, radius_damage);
	forward[0] = forward[0] - 6;

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_ROCKET | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 20;
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
		//ent->client->pers.inventory[ent->client->ammo_index];//--;
}

void Weapon_RocketLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {25, 33, 42, 50, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 12, 50, 54, pause_frames, fire_frames, Weapon_RocketLauncher_Fire);
}


/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

void Blaster_Fire (edict_t *ent, vec3_t g_offset, int damage, qboolean hyper, int effect)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	offset;
	vec3_t mine = {0, 1, 2};
	damage = 0;
	if (is_quad)
		damage *= 4;
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 8, ent->viewheight-8);
	VectorAdd (offset, g_offset, offset);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_blaster (ent, start, forward, damage, 330, effect, hyper);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	if (hyper)
		gi.WriteByte (MZ_HYPERBLASTER | is_silenced);
	else
		gi.WriteByte (MZ_BLASTER | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}


void Weapon_Blaster_Fire (edict_t *ent)
{
	int		damage;

	if (deathmatch->value)
		damage = 15;
	else
		damage = 10;
	Blaster_Fire (ent, vec3_origin, damage, false, EF_BLASTER);
	ent->client->ps.gunframe++;
}

void Weapon_Blaster (edict_t *ent)
{
	static int	pause_frames[]	= {19, 32, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Blaster_Fire);
}

int num = 0;
void Weapon_HyperBlaster_Fire (edict_t *ent)
{

	/*float	rotation;
	vec3_t	offset;
	int		effect;
	int		damage;

	ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe++;
	}
	else
	{
		if (! ent->client->pers.inventory[ent->client->ammo_index] )
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			NoAmmoWeaponChange (ent);
		}
		else
		{
			rotation = (ent->client->ps.gunframe - 5) * 2*M_PI/6;
			offset[0] = -4 * sin(rotation);
			offset[1] = 0;
			offset[2] = 4 * cos(rotation);

			if ((ent->client->ps.gunframe == 6) || (ent->client->ps.gunframe == 9))
				effect = EF_HYPERBLASTER;
			else
				effect = 0;
			if (deathmatch->value)
				damage = 15;
			else
				damage = 20;
			Blaster_Fire (ent, offset, damage, true, effect);
			if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
				ent->client->pers.inventory[ent->client->ammo_index]--;

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - 1;
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - 1;
				ent->client->anim_end = FRAME_attack8;
			}
		}

		ent->client->ps.gunframe++;
		if (ent->client->ps.gunframe == 12 && ent->client->pers.inventory[ent->client->ammo_index])
			ent->client->ps.gunframe = 6;
	}

	if (ent->client->ps.gunframe == 12)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		ent->client->weapon_sound = 0;
	}*/
	    vec3_t end,forward;
        trace_t tr;
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 6) // requires 10 cells
		{
			gi.cprintf (ent, PRINT_HIGH, "You need 6 mana to use Suck\n"); // Notify them
			//ent->client->ps.gunframe = 32;
			//ent->client->ps.gunframe++;
			ent->client->ps.gunframe++;
			return; // Stop the command from going
		}
		//ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 1;
        VectorCopy(ent->s.origin, end);
        AngleVectors (ent->client->v_angle, forward, NULL, NULL);
        end[0]=end[0]+forward[0]*250;
        end[1]=end[1]+forward[1]*250;
        end[2]=end[2]+forward[2]*250;
		ent->client->ps.gunframe++;
		num++;
		if(num == 6)
		{
			ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 6;
			num = 0;
				if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
		}
        tr = gi.trace (ent->s.origin, NULL, NULL, end, ent, MASK_SHOT);
        if(tr.ent != NULL) 
        {
              ent->enemy=tr.ent;
              parasite_drain_attack2(ent);
        }
}

static qboolean parasite_drain_attack_ok (vec3_t start, vec3_t end)
{
	vec3_t	dir, angles;

	// check for max distance
	VectorSubtract (start, end, dir);
	if (VectorLength(dir) > 256)
		return false;

	// check for min/max pitch
	vectoangles (dir, angles);
	if (angles[0] < -180)
		angles[0] += 360;
	if (fabs(angles[0]) > 30)
		return false;

	return true;
}

//#define FRAME_drain03           41
//#define FRAME_drain04           42
void parasite_drain_attack2 (edict_t *self)
{
	int sound_impact = gi.soundindex("parasite/paratck2.wav");
	int sound_suck = gi.soundindex("parasite/paratck3.wav");
	vec3_t	offset, start, f, r, end, dir;
	trace_t	tr;
	int damage;

	AngleVectors (self->s.angles, f, r, NULL);
	VectorSet (offset, 24, 0, 6);
	G_ProjectSource (self->s.origin, offset, f, r, start);

	VectorCopy (self->enemy->s.origin, end);
	if (!parasite_drain_attack_ok(start, end))
	{
		end[2] = self->enemy->s.origin[2] + self->enemy->maxs[2] - 8;
		if (!parasite_drain_attack_ok(start, end))
		{
			end[2] = self->enemy->s.origin[2] + self->enemy->mins[2] + 8;
			if (!parasite_drain_attack_ok(start, end))
				return;
		}
	}
	VectorCopy (self->enemy->s.origin, end);

	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if (tr.ent != self->enemy)
		return;

	if (self->s.frame == 41)
	{
		damage = 4;
		gi.sound (self->enemy, CHAN_AUTO, sound_impact, 1, ATTN_NORM, 0);
	}
	else
	{
		if (self->s.frame == 42)
			gi.sound (self, CHAN_WEAPON, sound_suck, 1, ATTN_NORM, 0);
		damage = 2;
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_PARASITE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	VectorSubtract (start, end, dir);
	T_Damage (self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin, damage, 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);
	self->health=self->health+damage;
}


void Weapon_HyperBlaster (edict_t *ent)
{
	static int	pause_frames[]	= {0};
	static int	fire_frames[]	= {6, 7, 8, 9, 10, 11, 0};

	Weapon_Generic (ent, 5, 20, 49, 53, pause_frames, fire_frames, Weapon_HyperBlaster_Fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/

void Machinegun_Fire (edict_t *ent)
{
	int	i;
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		angles;
	int			damage = 8;
	int			kick = 2;
	vec3_t		offset;

	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->ps.gunframe == 5)
		ent->client->ps.gunframe = 4;
	else
		ent->client->ps.gunframe = 5;

	if (ent->client->pers.inventory[ent->client->ammo_index] < 1)
	{
		ent->client->ps.gunframe = 6;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (i=1 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}
	ent->client->kick_origin[0] = crandom() * 0.35;
	ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5;

	// raise the gun as it is firing
	if (!deathmatch->value)
	{
		ent->client->machinegun_shots++;
		if (ent->client->machinegun_shots > 9)
			ent->client->machinegun_shots = 9;
	}

	// get start / end positions
	VectorAdd (ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors (angles, forward, right, NULL);
	VectorSet(offset, 0, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	//fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);
	//qboolean hyper = false;
	//int effect = 0;
	fire_blaster (ent, start, forward, 0, 1000, 0, false);
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_MACHINEGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	//PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index];

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Machinegun (edict_t *ent)
{
	static int	pause_frames[]	= {23, 45, 0};
	static int	fire_frames[]	= {4, 5, 0};

	Weapon_Generic (ent, 3, 5, 45, 49, pause_frames, fire_frames, Machinegun_Fire);
}

void Chaingun_Fire (edict_t *ent)
{
	int			i;
	int			shots;
	vec3_t		start;
	vec3_t		forward, right, up;
	float		r, u;
	vec3_t		offset;
	int			damage;
	int			kick = 2;

	if (deathmatch->value)
		damage = 6;
	else
		damage = 8;

	if (ent->client->ps.gunframe == 5)
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

	if ((ent->client->ps.gunframe == 14) && !(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe = 32;
		ent->client->weapon_sound = 0;
		return;
	}
	else if ((ent->client->ps.gunframe == 21) && (ent->client->buttons & BUTTON_ATTACK)
		&& ent->client->pers.inventory[ent->client->ammo_index])
	{
		ent->client->ps.gunframe = 15;
	}
	else
	{
		ent->client->ps.gunframe++;
	}

	if (ent->client->ps.gunframe == 22)
	{
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}
	else
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}

	if (ent->client->ps.gunframe <= 9)
		shots = 1;
	else if (ent->client->ps.gunframe <= 14)
	{
		if (ent->client->buttons & BUTTON_ATTACK)
			shots = 2;
		else
			shots = 1;
	}
	else
		shots = 3;

	if (ent->client->pers.inventory[ent->client->ammo_index] < shots)
		shots = ent->client->pers.inventory[ent->client->ammo_index];

	if (!shots)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (i=0 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}

	for (i=0 ; i<shots ; i++)
	{
		// get start / end positions
		AngleVectors (ent->client->v_angle, forward, right, up);
		r = 7 + crandom()*4;
		u = crandom()*4;
		VectorSet(offset, 0, r, u + ent->viewheight-8);
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

		fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
	}

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte ((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index] -= shots;
}


void Weapon_Chaingun (edict_t *ent)
{
	static int	pause_frames[]	= {38, 43, 51, 61, 0};
	static int	fire_frames[]	= {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0};

	Weapon_Generic (ent, 4, 31, 61, 64, pause_frames, fire_frames, Chaingun_Fire);
}


/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void weapon_shotgun_fire (edict_t *ent)
{
	/*vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage = 4;
	int			kick = 8;

	if (ent->client->ps.gunframe == 9)
	{
		ent->client->ps.gunframe++;
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	if (deathmatch->value)
		fire_shotgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_DEATHMATCH_SHOTGUN_COUNT, MOD_SHOTGUN);
	else
		fire_shotgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_SHOTGUN_COUNT, MOD_SHOTGUN);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;*/
//=================
//Cmd_Pull_f
//Added by Paril for Push/Pull
//=================
//*/
 vec3_t  start;
 vec3_t  forward;
 vec3_t  end;
 trace_t tr;
 vec3_t offset;
 vec3_t right;

if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 5) // requires 10 cells
{
			gi.cprintf (ent, PRINT_HIGH, "You need 5 mana to use Push\n"); // Notify them
			ent->client->ps.gunframe = 9;
			ent->client->ps.gunframe++;
			//ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))];
    return; // Stop the command from going
}
 ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 5;
 	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
 VectorCopy(ent->s.origin, start); // Copy your location
 start[2] += ent->viewheight; // vector for start is at your height of view
 AngleVectors(ent->client->v_angle, forward, NULL, NULL); // Angles
 VectorMA(start, 8192, forward, end); // How far will the line go?
 tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT); // Trace the line
 gi.sound (ent, CHAN_AUTO, gi.soundindex ("items/damage2.wav"), 1, ATTN_NORM, 0);
 ent->client->ps.gunframe = 9;
 ent->client->ps.gunframe++;

 //rail gun effect
 	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, 0, 0);
 	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(forward, 5000, forward); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(forward, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
 return;
}

void Weapon_Shotgun (edict_t *ent)
{
	static int	pause_frames[]	= {22, 28, 34, 0};
	static int	fire_frames[]	= {8, 9, 0};

	Weapon_Generic (ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}


void weapon_supershotgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		storage;
	vec3_t		forward, right;
	vec3_t		offset;
	vec3_t		v;

	 vec3_t  end;
	 trace_t tr;
	//int			damage = 6;
	int				damage = 4;
	int			kick = 12;

	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 20) // requires 10 cells
{
			gi.cprintf (ent, PRINT_HIGH, "You need 20 mana to use Super Shotgun\n"); // Notify them
			ent->client->ps.gunframe++;
    return; // Stop the command from going
}
	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	v[PITCH] = ent->client->v_angle[PITCH];
	v[YAW]   = ent->client->v_angle[YAW] - 5;
	v[ROLL]  = ent->client->v_angle[ROLL];
	AngleVectors (v, forward, NULL, NULL);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2, MOD_SSHOTGUN);
	v[YAW]   = ent->client->v_angle[YAW] + 5;
	AngleVectors (v, forward, NULL, NULL);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2, MOD_SSHOTGUN);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SSHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	VectorCopy(ent->s.origin, start); // Copy your location
	start[2] += ent->viewheight; // vector for start is at your height of view
	AngleVectors(ent->client->v_angle, forward, NULL, NULL); // Angles
	VectorMA(start, 8192, forward, end); // How far will the line go?
	tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT); // Trace the line
	gi.sound (ent, CHAN_AUTO, gi.soundindex ("items/damage2.wav"), 1, ATTN_NORM, 0);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);
 //rail gun effect
 	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, 0, 0);

	storage[0] = start[0];
	storage[1] = start[1];
	storage[2] = start[2];

	start[0] = start[0] - 9;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(forward, 5000, forward); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(forward, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
		start[0] = start[0] - 9;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
		start[0] = start[0] - 9;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
		start[0] = start[0] - 9;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	start[0] = start[0] + 18;
	fire_rail (ent, start, forward, 500, 10);
	 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(start, 5000, start); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(start, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }

	 /*
	storage[0] = storage[0] - 9;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
		storage[0] = storage[0] - 9;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
		storage[0] = storage[0] - 9;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
		storage[0] = storage[0] - 9;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);
	storage[0] = storage[0] - 18;
	fire_rail (ent, storage, forward, 0, 0);*/
 	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(forward, 5000, forward); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(forward, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		 ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 20;
		if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
		//ent->client->pers.inventory[ent->client->ammo_index] -= 2;
	return;
}

void Weapon_SuperShotgun (edict_t *ent)
{
	static int	pause_frames[]	= {29, 42, 57, 0};
	static int	fire_frames[]	= {7, 0};

	Weapon_Generic (ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}



/*
======================================================================

RAILGUN

======================================================================
*/

void weapon_railgun_fire (edict_t *ent)
{
	/*vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage;
	int			kick;

	if (deathmatch->value)
	{	// normal damage is too extreme in dm
		damage = 100;
		kick = 200;
	}
	else
	{
		damage = 150;
		kick = 250;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, damage, kick);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;*/ 
	//johnny b
/*
=================
Cmd_Pull_f
Added by Paril for Push/Pull
=================
*/
 vec3_t  start;
 vec3_t  forward;
 vec3_t  end;
 trace_t tr;
 vec3_t offset;
 vec3_t right;

if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 5) // requires 10 cells
{
			gi.cprintf (ent, PRINT_HIGH, "You need 5 mana to use Push\n"); // Notify them
			ent->client->ps.gunframe++;
    return; // Stop the command from going
}
 ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 5;
 	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
 VectorCopy(ent->s.origin, start); // Copy your location
 start[2] += ent->viewheight; // vector for start is at your height of view
 AngleVectors(ent->client->v_angle, forward, NULL, NULL); // Angles
 VectorMA(start, 8192, forward, end); // How far will the line go?
 tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT); // Trace the line
 gi.sound (ent, CHAN_AUTO, gi.soundindex ("items/damage2.wav"), 1, ATTN_NORM, 0);
 ent->client->ps.gunframe++;

 //rail gun effect
 	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, 0, 0);
 	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);
 if ( tr.ent && ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client)) ) // Trace the line
 {
        VectorScale(forward, -5000, forward); //Where to hit? Edit -5000 to whatever you like the push to be
        VectorAdd(forward, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
 }
 return;
}


void Weapon_Railgun (edict_t *ent)
{
	static int	pause_frames[]	= {56, 0};
	static int	fire_frames[]	= {4, 0};

	Weapon_Generic (ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}


/*
======================================================================

BFG10K

======================================================================
*/

void weapon_bfg_fire (edict_t *ent)
{
	/*vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;
	float	damage_radius = 1000;

	if (deathmatch->value)
		damage = 200;
	else
		damage = 500;

	if (ent->client->ps.gunframe == 9)
	{
		// send muzzle flash
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_BFG | is_silenced);
		gi.multicast (ent->s.origin, MULTICAST_PVS);

		ent->client->ps.gunframe++;

		PlayerNoise(ent, start, PNOISE_WEAPON);
		return;
	}

	// cells can go down during windup (from power armor hits), so
	// check again and abort firing if we don't have enough now
	if (ent->client->pers.inventory[ent->client->ammo_index] < 50)
	{
		ent->client->ps.gunframe++;
		return;
	}

	if (is_quad)
		damage *= 4;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);

	// make a big pitch kick with an inverse fall
	ent->client->v_dmg_pitch = -40;
	ent->client->v_dmg_roll = crandom()*8;
	ent->client->v_dmg_time = level.time + DAMAGE_TIME;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_bfg (ent, start, forward, damage, 400, damage_radius);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index] -= 50;*/
	vec3_t  start;
	vec3_t  forward;
	vec3_t  end;
	trace_t tr;
	trace_t tr2;
	trace_t tr3;

	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
	{
		gi.cprintf (ent, PRINT_HIGH, "You need 1 mana to use Teleport\n"); // Notify them
			ent->client->ps.gunframe = 17;
			ent->client->ps.gunframe++;
		return; // Stop the command from going
	}
    ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] -= 1;
 	if (ent->client->pers.inventory[ITEM_INDEX(FindItem ("slugs"))] < 1) // requires 10 cells
		ent->client->pers.inventory[ent->client->ammo_index] = 1;
	VectorCopy(ent->s.origin, start); // Copy your location
	start[2] += ent->viewheight; // vector for start is at your height of view
	AngleVectors(ent->client->v_angle, forward, NULL, NULL); // Angles
	VectorMA(start, 475, forward, end); // How far will the line go?
	//int	content_mask = MASK_JOHN;
	tr = gi.trace(start, NULL, NULL, end, ent, MASK_ALL); // Trace the line
	VectorMA(start, 435, forward, end); // How far will the line go?
	tr2 = gi.trace(start, NULL, NULL, end, ent, MASK_ALL); // Trace the line
	//tr3.endpos[2] = tr2.endpos[2] - (tr3.endpos[2] - tr2.endpos[2]);
	//tr3.endpos[1] = tr2.endpos[1] - (tr3.endpos[1] - tr2.endpos[1]);
	//tr3.endpos[0] = tr2.endpos[0] - (tr3.endpos[0] - tr2.endpos[0]);
	gi.dprintf("x: %i\n", tr.endpos[0]);
	gi.dprintf("y: %i\n", tr.endpos[1]);
	gi.dprintf("z: %i\n", tr.endpos[2]);

	//tr.endpos[2] = tr.endpos[2] - 25;
	//tr.endpos[1] = tr.endpos[1] - 25;
	if(tr.endpos[1] > 0)
		tr.endpos[1] = tr.endpos[1] - 30;
	if(tr.endpos[1] < 0)
		tr.endpos[1] = tr.endpos[1] + 30;
	if(tr.endpos[0] > 0)
		tr.endpos[0] = tr.endpos[0] - 30;
	if(tr.endpos[0] < 0)
		tr.endpos[0] = tr.endpos[0] + 30;
	if(tr.endpos[2] > 0)
		tr.endpos[2] = tr.endpos[2] - 30;
	if(tr.endpos[2] < 0)
		tr.endpos[2] = tr.endpos[2] + 30;
	//tr.endpos = tr.endpos - 10;
	//tr.ent->svflags;
	gi.sound (ent, CHAN_AUTO, gi.soundindex ("items/damage2.wav"), 1, ATTN_NORM, 0);
	ent->client->ps.gunframe = 17;
	ent->client->ps.gunframe++;
	//if ( tr.ent && ((tr.ent->svflags) || (tr.ent->client)) ) // Trace the line
	//{
			ent->s.event = EV_PLAYER_TELEPORT;
			//VectorCopy(end, ent->s.origin);
			VectorCopy(tr.endpos, ent->s.origin);
			//VectorCopy(tr2.endpos - (tr3.endpos - tr2.endpos), end->s.origin);
			//ent->s.origin = end;
		    //VectorScale(forward, -5000, forward); //Where to hit? Edit -5000 to whatever you like the push to be
			//VectorAdd(forward, tr.ent->velocity, tr.ent->velocity); // Adding velocity vectors
	//}
}

void Weapon_BFG (edict_t *ent)
{
	static int	pause_frames[]	= {39, 45, 50, 55, 0};
	static int	fire_frames[]	= {9, 17, 0};

	Weapon_Generic (ent, 8, 32, 55, 58, pause_frames, fire_frames, weapon_bfg_fire);
}

/*
=======================
Punching/Melee
=======================
*/

void Null_Fire(edict_t *ent)
{
	int	i;
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		angles;
	int			damage = 999; //change to whatever
	int			kick = 10; //ditto here
	vec3_t		offset;

	if (ent->client->ps.gunframe == 11) //rename 11 to after you're attack frame
	{
		ent->client->ps.gunframe++;
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8 );
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start); //where does the hit start from?

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	// get start / end positions
	VectorAdd (ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors (angles, forward, right, NULL);
	VectorSet(offset, 0, 8, ent->viewheight-8 );
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_punch (ent, start, forward, 45, damage, 200, 1, MOD_PUNCH); // yep, matches the fire_ function	

	ent->client->ps.gunframe++; //NEEDED
	PlayerNoise(ent, start, PNOISE_WEAPON); //NEEDED

//	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
//		ent->client->pers.inventory[ent->client->ammo_index]--; // comment these out to prevent the Minus NULL Ammo bug
}

void Weapon_Null (edict_t *ent)
{
	static int	pause_frames[]	= {10, 21, 0};
	static int	fire_frames[]	= {6, 0}; // Frame stuff here

	Weapon_Generic (ent, 3, 9, 22, 24, pause_frames, fire_frames, Null_Fire);
}

//======================================================================

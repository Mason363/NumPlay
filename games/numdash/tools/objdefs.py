"""Object catalogue shared by the level compiler and the C runtime.

Hitboxes and draw layers follow Geometry Dash 2.x: hitbox sizes are in GD
units before rotation; layer ranks encode GD's z-layer / blending / z-order
sort so that decorations, glows, the player, portals and blocks overlap in
the same order as the original game.
"""

# Collision kinds
NONE, SOLID, HAZARD, SPECIAL = range(4)
# Special behaviours
SP = ['NONE', 'PAD_Y', 'PAD_P', 'PAD_B', 'ORB_Y', 'ORB_P', 'ORB_B', 'GRAV_N', 'GRAV_F',
      'PORTAL_CUBE', 'PORTAL_SHIP', 'COIN', 'PORTAL_BALL', 'PORTAL_UFO', 'SIZE_MINI', 'SIZE_NORMAL',
      'PORTAL_WAVE', 'PORTAL_ROBOT', 'SPEED_0', 'SPEED_1', 'SPEED_2', 'SPEED_3', 'DUAL_ON', 'DUAL_OFF',
      'TELEPORT', 'ORB_G', 'KEY', 'TOUCH', 'PORTAL_SPIDER', 'PORTAL_SWING', 'ORB_DASH', 'ORB_DASH_G', 'ORB_SPIDER',
      'ORB_R', 'PAD_R', 'ORB_T', 'TELEPORT2', 'ITEM', 'FORCE', 'FORCE_CIRCLE', 'STOP_DASH',
      'ARM_HEAD', 'ARM_SLIDE', 'ARM_FLIP', 'ARM_NOAUTO', 'MIRROR_ON', 'MIRROR_OFF']
# Colour types for draw parts
CT = ['OBJ', 'BLACK', 'WHITE', 'P1ADD', 'P2ADD', 'LBG', 'GLOW', 'GLOW_Y', 'GLOW_B', 'GLOW_P', 'BASE', 'DETAIL', 'BASE_D',
      'BASE_L', 'DETAIL_D', 'RAIN0', 'RAIN1', 'RAIN2', 'RAIN3', 'RAIN4', 'RAIN5']
# Layer ranks (drawing order); the player is drawn at LAYER_PLAYER.
LAYERS = ['B4', 'DECO_BACK', 'RODS', 'ROD_BALLS', 'DETAIL', 'SPECIAL_GLOW', 'SPECIAL', 'PORTAL_BACK',
          'BLOCK_GLOW', 'PLAYER', 'COIN', 'PORTAL_FRONT', 'FILL', 'BLOCK', 'T2', 'T3']
# Object animations (ObjDef.anim)
ANIM = {'NONE': 0, 'SAW': 1, 'SPIN': 2, 'INVIS': 3, 'FLAME': 4, 'CHOMP': 5}
# Part flags
F_PULSE, F_RANDOM3, F_COIN, F_ANIM, F_QUAD, F_HALF = 1, 2, 4, 8, 16, 32

# name, gd ids, collision, hitbox w, h, special, editor y offset, parts
# part: (sprite, dx, dy, layer, colour type, flags)
OBJECTS = [
    ('BLOCK', [1], SOLID, 30, 30, 'NONE', 0, [('block', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_all', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_T', [2], SOLID, 30, 30, 'NONE', 0, [('grid_t', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_t', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_TL', [3], SOLID, 30, 30, 'NONE', 0, [('grid_tl', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_tl', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_C', [4], SOLID, 30, 30, 'NONE', 0, [('grid_c', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_c', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_LTR', [6], SOLID, 30, 30, 'NONE', 0, [('grid_ltr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_ltr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_LR', [7], SOLID, 30, 30, 'NONE', 0, [('grid_lr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_lr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('GRID_DECO', [5], NONE, 0, 0, 'NONE', 0, [('grid_none', 0, 0, 'DECO_BACK', 'OBJ', 0)]),
    ('DIAG_DECO', [73], NONE, 0, 0, 'NONE', 0, [('diag_block', 0, 0, 'DECO_BACK', 'OBJ', 0)]),
    ('PLANK', [40], SOLID, 30, 14, 'NONE', 8, [('plank', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_plank', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('SLAB0', [62], SOLID, 30, 16, 'NONE', 7, [('slab0', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('SLAB1', [65], SOLID, 30, 16, 'NONE', 7, [('slab1', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('SLAB2', [66], SOLID, 30, 16, 'NONE', 7, [('slab2', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('SLAB3', [68], SOLID, 30, 16, 'NONE', 7, [('slab3', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('SPIKE', [8], HAZARD, 6, 12, 'NONE', 0, [('spike', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_spike', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('SPIKE_SMALL', [39], HAZARD, 6, 5.6, 'NONE', -9, [('spike_small', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_spike_small', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('SPIKE_MED', [103], HAZARD, 4, 7.6, 'NONE', -6, [('spike_med', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_spike_med', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('PIT', [9], HAZARD, 9, 10.8, 'NONE', -13, [('pit0', 0, 0, 'BLOCK', 'BLACK', F_RANDOM3)]),
    ('ROD1', [15], NONE, 0, 0, 'NONE', 6, [('rod1', 0, 0, 'RODS', 'BLACK', 0), ('rod_ball', 0, 28, 'ROD_BALLS', 'P1ADD', F_PULSE)]),
    ('ROD2', [16], NONE, 0, 0, 'NONE', -1, [('rod2', 0, 0, 'RODS', 'BLACK', 0), ('rod_ball', 0, 20.5, 'ROD_BALLS', 'P1ADD', F_PULSE)]),
    ('ROD3', [17], NONE, 0, 0, 'NONE', -8, [('rod3', 0, 0, 'RODS', 'BLACK', 0), ('rod_ball', 0, 13.5, 'ROD_BALLS', 'P1ADD', F_PULSE)]),
    ('DSPIKES1', [18], NONE, 0, 0, 'NONE', 4, [('dspikes1', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('DSPIKES2', [19], NONE, 0, 0, 'NONE', 4, [('dspikes2', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('DSPIKES3', [20], NONE, 0, 0, 'NONE', -2, [('dspikes3', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('DSPIKES4', [21], NONE, 0, 0, 'NONE', -8, [('dspikes4', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('CHAIN', [41], NONE, 0, 0, 'NONE', -12, [('chain', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('CHAIN_SHORT', [110], NONE, 0, 0, 'NONE', -13, [('chain_short', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('STAR', [54], NONE, 0, 0, 'NONE', 0, [('star', 0, 0, 'DETAIL', 'P2ADD', 0)]),
    ('PAD_Y', [35], SPECIAL, 25, 4, 'PAD_Y', -13, [('pad_yellow', 0, 0, 'SPECIAL', 'WHITE', 0), ('glow_pad_yellow', 0, 2, 'SPECIAL_GLOW', 'GLOW_Y', 0)]),
    ('PAD_B', [67], SPECIAL, 25, 6, 'PAD_B', -12, [('pad_blue', 0, 0, 'SPECIAL', 'WHITE', 0), ('glow_pad_blue', 0, 2, 'SPECIAL_GLOW', 'GLOW_B', 0)]),
    ('PAD_P', [140], SPECIAL, 25, 5, 'PAD_P', -13, [('pad_pink', 0, 0, 'SPECIAL', 'WHITE', 0), ('glow_pad_pink', 0, 2, 'SPECIAL_GLOW', 'GLOW_P', 0)]),
    ('ORB_Y', [36], SPECIAL, 36, 36, 'ORB_Y', 0, [('orb_yellow', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_yellow', 0, 0, 'SPECIAL_GLOW', 'GLOW_Y', 0)]),
    ('ORB_P', [141], SPECIAL, 36, 36, 'ORB_P', 0, [('orb_pink', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_pink', 0, 0, 'SPECIAL_GLOW', 'GLOW_P', 0)]),
    ('ORB_B', [84], SPECIAL, 36, 36, 'ORB_B', 0, [('orb_blue', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_blue', 0, 0, 'SPECIAL_GLOW', 'GLOW_B', 0)]),
    ('GRAV_N', [10], SPECIAL, 25, 75, 'GRAV_N', 0, [('portal_grav_blue_front', 4, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_grav_blue_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('GRAV_F', [11], SPECIAL, 25, 75, 'GRAV_F', 0, [('portal_grav_yellow_front', 4, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_grav_yellow_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('PORTAL_CUBE', [12], SPECIAL, 34, 86, 'PORTAL_CUBE', 0, [('portal_cube_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_cube_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('PORTAL_SHIP', [13], SPECIAL, 34, 86, 'PORTAL_SHIP', 0, [('portal_ship_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_ship_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('COIN', [1329, 142], SPECIAL, 40, 40, 'COIN', 0, [('coin0', 0, 0, 'COIN', 'WHITE', F_COIN)]),
    # added after the first release: new objects go last, custom levels store these indices
    ('PORTAL_BALL', [47], SPECIAL, 34, 86, 'PORTAL_BALL', 0, [('portal_ball_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_ball_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    # ---- Clubstep (level 14): 1.x blocks, saws, monsters' parts and deco
    ('UFO_PORTAL', [111], SPECIAL, 34, 86, 'PORTAL_UFO', 0, [('portal_ufo_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_ufo_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('SIZE_MINI', [101], SPECIAL, 31, 90, 'SIZE_MINI', 0, [('portal_mini_front', 4, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_mini_back', -5, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('SIZE_NORMAL', [99], SPECIAL, 31, 90, 'SIZE_NORMAL', 0, [('portal_big_front', 4, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_big_back', -5, 0, 'PORTAL_BACK', 'WHITE', 0)]),
    ('BLACK_T', [91], SOLID, 30, 30, 'NONE', 0, [('prog:blk_t', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_t', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BLACK_TL', [92], SOLID, 30, 30, 'NONE', 0, [('prog:blk_tl', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_tl', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BLACK_C', [93], SOLID, 30, 30, 'NONE', 0, [('prog:blk_c', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('BLACK_IN', [94], SOLID, 30, 30, 'NONE', 0, [('prog:blk_in', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('BLACK_LTR', [95], SOLID, 30, 30, 'NONE', 0, [('prog:blk_ltr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_ltr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BLACK_LR', [96], SOLID, 30, 30, 'NONE', 0, [('prog:blk_lr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_lr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BEVEL_ALL', [69], SOLID, 30, 30, 'NONE', 0, [('bev_body', 0, 0, 'BLOCK', 'BLACK', 0), ('prog:edge_ltrb', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_all', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BEVEL_T', [70], SOLID, 30, 30, 'NONE', 0, [('bev_body', 0, 0, 'BLOCK', 'BLACK', 0), ('prog:edge_t', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_t', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BEVEL_C', [72], SOLID, 30, 30, 'NONE', 0, [('bev_body', 0, 0, 'BLOCK', 'BLACK', 0), ('prog:edge_c', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_c', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BEVEL_LR', [75], SOLID, 30, 30, 'NONE', 0, [('bev_body', 0, 0, 'BLOCK', 'BLACK', 0), ('prog:edge_lr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_lr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_ALL', [116], SOLID, 30, 30, 'NONE', 0, [('prog:brick_all', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_all', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_T', [117], SOLID, 30, 30, 'NONE', 0, [('prog:brick_t', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_t', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_TL', [118], SOLID, 30, 30, 'NONE', 0, [('prog:brick_tl', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_tl', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_C', [119], SOLID, 30, 30, 'NONE', 0, [('prog:brick_c', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_c', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_IN', [120], NONE, 0, 0, 'NONE', 0, [('prog:brick_in', 0, 0, 'DECO_BACK', 'OBJ', 0)]),
    ('BRICK_LTR', [121], SOLID, 30, 30, 'NONE', 0, [('prog:brick_ltr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_ltr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('BRICK_LR', [122], SOLID, 30, 30, 'NONE', 0, [('prog:brick_lr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_lr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_T', [161], SOLID, 30, 30, 'NONE', 0, [('prog:stone_t', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_t', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_TL', [162], SOLID, 30, 30, 'NONE', 0, [('prog:stone_tl', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_tl', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_C', [163], SOLID, 30, 30, 'NONE', 0, [('prog:stone_c', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_c', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_IN', [164], NONE, 0, 0, 'NONE', 0, [('prog:stone_in', 0, 0, 'DECO_BACK', 'OBJ', 0)]),
    ('STONE_LTR', [165], SOLID, 30, 30, 'NONE', 0, [('prog:stone_ltr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_ltr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_LR', [166], SOLID, 30, 30, 'NONE', 0, [('prog:stone_lr', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_lr', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_CREN', [167], SOLID, 30, 30, 'NONE', 0, [('prog:stone_cren', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_c', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('STONE_CHECK', [169], SOLID, 30, 30, 'NONE', 0, [('prog:stone_check', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_all', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('METAL_SLAB', [170], SOLID, 30, 21, 'NONE', 5, [('prog:metal_slab', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('METAL_SLAB2', [171], SOLID, 30, 21, 'NONE', 5, [('prog:metal_slab2', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('FAKE_SPIKE', [191], NONE, 0, 0, 'NONE', 0, [('fake_spike', 0, 0, 'DECO_BACK', 'BLACK', 0)]),
    ('FAKE_SQUARE', [193], NONE, 0, 0, 'NONE', 0, [('prog:fake_square', 0, 0, 'DECO_BACK', 'OBJ', 0)]),
    ('FAKE_SPIKE_H', [198], NONE, 0, 0, 'NONE', 0, [('fake_spike_h', 0, 0, 'DECO_BACK', 'BLACK', 0)]),
    ('FAKE_SPIKE_S', [199], NONE, 0, 0, 'NONE', 0, [('fake_spike_s', 0, 0, 'DECO_BACK', 'BLACK', 0)]),
    ('GROUND_SPIKES', [61], HAZARD, 9, 7.2, 'NONE', -8, [('ground_spikes', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('ICE_SPIKE', [177], HAZARD, 6, 12, 'NONE', 0, [('ice_spike', 0, 0, 'BLOCK', 'OBJ', 0), ('glow_spike', 0, 0, 'BLOCK_GLOW', 'GLOW', 0)]),
    ('ICE_SPIKE_HALF', [178], HAZARD, 6, 6.4, 'NONE', -8, [('ice_spike_half', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('ICE_SPIKE_SMALL', [179], HAZARD, 4, 8, 'NONE', -5, [('ice_spike_small', 0, 0, 'BLOCK', 'OBJ', 0)]),
    ('INVIS_SPIKE', [144], HAZARD, 6, 12, 'NONE', 0, [('invis_spike', 0, 0, 'BLOCK', 'OBJ', 0)], {'anim': 'INVIS'}),
    ('INVIS_SPIKE_S', [145], HAZARD, 4, 7.6, 'NONE', -6, [('invis_spike_s', 0, 0, 'BLOCK', 'OBJ', 0)], {'anim': 'INVIS'}),
    ('INVIS_SQUARE', [146], SOLID, 30, 30, 'NONE', 0, [('invis_square', 0, 0, 'BLOCK', 'OBJ', 0)], {'anim': 'INVIS'}),
    ('SAW_BIG', [88], HAZARD, 32.3, 32.3, 'NONE', 0, [('saw_big', 0, 0, 'BLOCK', 'BLACK', F_QUAD)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('SAW_MED', [89], HAZARD, 21.6, 21.6, 'NONE', 0, [('saw_med', 0, 0, 'BLOCK', 'BLACK', F_QUAD)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('SAW_SMALL', [98], HAZARD, 12, 12, 'NONE', 0, [('saw_small', 0, 0, 'BLOCK', 'BLACK', 0)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('BLADE_BIG', [183], HAZARD, 15.66, 15.66, 'NONE', 0, [('blade_big', 0, 0, 'BLOCK', 'OBJ', F_QUAD)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('BLADE_MED', [184], HAZARD, 20.4, 20.4, 'NONE', 0, [('blade_med', 0, 0, 'BLOCK', 'OBJ', 0)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('BLADE_SMALL', [185], HAZARD, 2.85, 2.85, 'NONE', 0, [('blade_small', 0, 0, 'BLOCK', 'OBJ', 0)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('OBLADE_BIG', [186], HAZARD, 32.3, 32.3, 'NONE', 0, [('oblade_big', 0, 0, 'BLOCK', 'OBJ', F_QUAD)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('OBLADE_MED', [187], HAZARD, 21.96, 21.96, 'NONE', 0, [('oblade_med', 0, 0, 'BLOCK', 'OBJ', F_QUAD)], {'shape': 'CIRCLE', 'anim': 'SAW'}),
    ('GEAR_L', [85], NONE, 0, 0, 'NONE', 0, [('gear_l', 0, 0, 'DETAIL', 'P1ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('GEAR_M', [86], NONE, 0, 0, 'NONE', 0, [('gear_m', 0, 0, 'DETAIL', 'P1ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('GEAR_S', [87], NONE, 0, 0, 'NONE', 0, [('gear_s', 0, 0, 'DETAIL', 'P1ADD', 0)], {'anim': 'SPIN'}),
    ('WHEEL_L', [137], NONE, 0, 0, 'NONE', 0, [('wheel_l', 0, 0, 'DETAIL', 'P2ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('WHEEL_M', [138], NONE, 0, 0, 'NONE', 0, [('wheel_m', 0, 0, 'DETAIL', 'P2ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('WHEEL_S', [139], NONE, 0, 0, 'NONE', 0, [('wheel_s', 0, 0, 'DETAIL', 'P2ADD', 0)], {'anim': 'SPIN'}),
    ('SPIKEWHEEL', [154], NONE, 0, 0, 'NONE', 0, [('spikewheel', 0, 0, 'DETAIL', 'P1ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('CARTWHEEL_L', [180], NONE, 0, 0, 'NONE', 0, [('cartwheel_l', 0, 0, 'DETAIL', 'P2ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('CARTWHEEL_M', [181], NONE, 0, 0, 'NONE', 0, [('cartwheel_m', 0, 0, 'DETAIL', 'P2ADD', F_QUAD)], {'anim': 'SPIN'}),
    ('CARTWHEEL_S', [182], NONE, 0, 0, 'NONE', 0, [('cartwheel_s', 0, 0, 'DETAIL', 'P2ADD', 0)], {'anim': 'SPIN'}),
    ('WIDE_CHAIN', [106], NONE, 0, 0, 'NONE', 0, [('wide_chain', 0, 0, 'DETAIL', 'P2ADD', 0)]),
    ('WIDE_CHAIN_S', [107], NONE, 0, 0, 'NONE', 0, [('wide_chain_s', 0, 0, 'DETAIL', 'P2ADD', 0)]),
    ('CLOUD_FADE_L', [48], NONE, 0, 0, 'NONE', 0, [('cloud_fade_l', 0, 0, 'DETAIL', 'P2ADD', F_HALF)]),
    ('CLOUD_FADE_S', [49], NONE, 0, 0, 'NONE', 0, [('cloud_fade_s', 0, 0, 'DETAIL', 'P2ADD', F_HALF)]),
    ('CLOUD_M', [129], NONE, 0, 0, 'NONE', 0, [('cloud_m', 0, 0, 'DETAIL', 'P1ADD', F_HALF)]),
    ('CLOUD_L', [130], NONE, 0, 0, 'NONE', 0, [('cloud_l', 0, 0, 'DETAIL', 'P1ADD', F_HALF)]),
    ('CLOUD_S', [131], NONE, 0, 0, 'NONE', 0, [('cloud_s', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('PULSE_DISC', [50], NONE, 0, 0, 'NONE', 0, [('pulse_disc', 0, 0, 'DETAIL', 'P2ADD', F_PULSE)]),
    ('PULSE_RING', [51], NONE, 0, 0, 'NONE', 0, [('pulse_ring', 0, 0, 'DETAIL', 'P2ADD', F_PULSE)]),
    ('PULSE_DIAMOND', [53], NONE, 0, 0, 'NONE', 0, [('pulse_diamond', 0, 0, 'DETAIL', 'P2ADD', F_PULSE)]),
    ('PULSE_ARROW', [132], NONE, 0, 0, 'NONE', 0, [('pulse_arrow', 0, 0, 'DETAIL', 'P2ADD', F_PULSE)]),
    ('PULSE_CROSS', [150], NONE, 0, 0, 'NONE', 0, [('pulse_cross', 0, 0, 'DETAIL', 'P2ADD', F_PULSE)]),
    ('SPIKEROD_L', [151], NONE, 0, 0, 'NONE', 0, [('spikerod_l', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('SPIKEROD_M', [152], NONE, 0, 0, 'NONE', 0, [('spikerod_m', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('SPIKEROD_S', [153], NONE, 0, 0, 'NONE', 0, [('spikerod_s', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('DIAMOND_ROD', [190], NONE, 0, 0, 'NONE', 0, [('diamond_rod', 0, 0, 'DETAIL', 'P2ADD', 0)]),
    ('WAVY', [157], NONE, 0, 0, 'NONE', 0, [('wavy', 0, 0, 'DETAIL', 'LBG', 0)]),
    ('WAVY_L', [158], NONE, 0, 0, 'NONE', 0, [('wavy_l', 0, 0, 'DETAIL', 'LBG', 0)]),
    ('WAVY_R', [159], NONE, 0, 0, 'NONE', 0, [('wavy_r', 0, 0, 'DETAIL', 'LBG', 0)]),
    ('DECO_BRICKS_L', [113], NONE, 0, 0, 'NONE', 0, [('prog:deco_bricks_l', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    ('DECO_BRICKS_M', [114], NONE, 0, 0, 'NONE', 0, [('prog:deco_bricks_m', 0, 0, 'DETAIL', 'P1ADD', 0)]),
    # ---- 2.0 levels. A touch-triggered trigger: fires when the player's box
    # overlaps its 30 x 30 box (style.arg = its index among the touch triggers)
    ('TOUCH', [], SPECIAL, 30, 30, 'TOUCH', 0, []),
]

# Level trigger objects become events rather than objects.
EV_COLOR, EV_FADE, EV_TRAIL = 1, 2, 3
COLOR_TRIGGERS = {29: 0, 30: 1, 104: 2, 105: 3}     # BG, G1, LINE, OBJ
FADE_TRIGGERS = {22: 0, 23: 1, 24: 2, 25: 3, 26: 4, 27: 5, 28: 6, 55: 7, 56: 8, 57: 9, 58: 10, 59: 11}
TRAIL_TRIGGERS = {32: 1, 33: 0}

# Palette offered by the level editor, in GD's build-tab order.
EDITOR = ['BLOCK', 'GRID_T', 'GRID_TL', 'GRID_LTR', 'GRID_LR', 'GRID_C', 'PLANK',
          'SPIKE', 'SPIKE_SMALL', 'SPIKE_MED', 'PIT', 'PAD_Y', 'PAD_P', 'PAD_B', 'ORB_Y',
          'PORTAL_SHIP', 'PORTAL_CUBE', 'PORTAL_BALL', 'GRAV_F', 'GRAV_N', 'COIN', 'GRID_DECO', 'ROD1', 'ROD2',
          'ROD3', 'DSPIKES3', 'DSPIKES4', 'CHAIN', 'STAR']

import math  # noqa: E402
import objdefs_dl  # noqa: E402  (Deadlocked's objects, after the ones above)
LOBJ_TYPES = len(OBJECTS) + 1       # types a custom level (LObj) may hold
assert LOBJ_TYPES <= 256
OBJECTS += objdefs_dl.OBJS
import objdefs_dash  # noqa: E402  (Dash's)
OBJECTS += objdefs_dash.OBJS
# Meltdown's and SubZero's, only in NumDash.nwa (ND_EXTRA_LEVELS): types from
# EXTRA_FIRST on; the other levels are compiled without their GD ids (EXTRA_IDS)
N_PROGS_BASE = len(objdefs_dl.PROGS)
import objdefs_ms  # noqa: E402
EXTRA_FIRST = len(OBJECTS) + 1
OBJECTS += objdefs_ms.OBJS
EXTRA_IDS = {gid for o in objdefs_ms.OBJS for gid in o[1]}

INDEX = {o[0]: i + 1 for i, o in enumerate(OBJECTS)}     # type 0 = none
BY_ID = {}
for i, o in enumerate(OBJECTS):
    for gid in o[1]:
        assert gid not in BY_ID, 'GD id %d listed twice' % gid
        BY_ID[gid] = i + 1

# Largest inflated chunk the runtime accepts (bytes); see level.c.
CHUNK_BYTES = 6000
# How far from its centre an object can reach (units), for streaming and
# collision windows. Types not listed use 45.
RADIUS = {INDEX[n]: r for n, r in [('DSPIKES1', 66), ('DSPIKES2', 54), ('CHAIN', 45), ('PORTAL_CUBE', 45), ('PORTAL_SHIP', 45),
                                     ('PORTAL_BALL', 45), ('GRAV_N', 40), ('GRAV_F', 40),
                                     ('SAW_BIG', 48), ('GEAR_L', 60), ('SPIKEWHEEL', 60), ('CARTWHEEL_L', 60), ('WHEEL_L', 60),
                                     ('CLOUD_L', 90), ('CLOUD_M', 60), ('CLOUD_FADE_L', 90), ('CLOUD_FADE_S', 60),
                                     ('DECO_BRICKS_L', 110), ('DECO_BRICKS_M', 80), ('WIDE_CHAIN', 50), ('SPIKEROD_L', 50),
                                     ('OBLADE_BIG', 48), ('DIAMOND_ROD', 50)]}


# ---------------------------------------------------------------- tile programs
# Blocks drawn at run time from a few rectangles (units, y up, centre origin):
# much smaller than a bitmap per block variant. The outline is the object
# colour, 2 units wide, like the bitmap blocks of the first levels.
CT_INDEX = {n: i for i, n in enumerate(CT)}


def R(x0, y0, x1, y1, ct='OBJ', a=1.0):
    return ('rect', x0, y0, x1, y1, ct, a)


def VG(x0, y0, x1, y1, ct, a_top, a_bottom):
    return ('vgrad', x0, y0, x1, y1, ct, a_top, a_bottom)


def edges(letters, w=2, h=15):
    out = []
    if 't' in letters: out.append(R(-15, h - w, 15, h))
    if 'b' in letters: out.append(R(-15, -h, 15, -h + w))
    if 'l' in letters: out.append(R(-15, -h + ('b' in letters) * w, -15 + w, h - ('t' in letters) * w))
    if 'r' in letters: out.append(R(15 - w, -h + ('b' in letters) * w, 15, h - ('t' in letters) * w))
    if 'c' in letters: out.append(R(-15, h - w, -15 + w, h))
    return out


def P(pts, ct='OBJ', a=1.0):
    """Convex polygon (units, y up)."""
    return ('poly', pts, ct, a)


def encode_prog(ops):
    out = bytearray()
    for op in ops:
        kind = op[0]
        if kind == 'poly':
            pts = op[1]
            assert 3 <= len(pts) <= 8, op
            big = max(max(abs(x), abs(y)) for x, y in pts) > 63
            out.append(3 | (0x80 if big else 0))
            out.append(len(pts))
            for x, y in pts:
                for v in (x, y):
                    c = int(round(v / 2 if big else v * 2))
                    assert -128 <= c <= 127, op
                    out.append(c & 255)
            out.append(CT_INDEX[op[2]])
            out.append(int(round(op[3] * 255)))
            continue
        big = max(abs(v) for v in op[1:5]) > 63
        coords = [int(round(v / 2 if big else v * 2)) for v in op[1:5]]
        assert all(-128 <= c <= 127 for c in coords), op
        out.append({'rect': 1, 'vgrad': 2}[kind] | (0x80 if big else 0))
        out += bytes(c & 255 for c in coords)
        out.append(CT_INDEX[op[5]])
        out.append(int(round(op[6] * 255)))
        if kind == 'vgrad':
            out.append(int(round(op[7] * 255)))
    out.append(0)
    return bytes(out)


FULL = R(-15, -15, 15, 15, 'BLACK', 1.0)


def brick_body():
    ops = [R(-15, -15, 15, 15, 'BLACK', 0.62)]
    for (x0, y0, x1, y1) in [(-13, 1.5, 13, 13), (-15, -13, -1.5, -1.5), (1.5, -13, 15, -1.5)]:
        ops.append(R(x0, y0, x1, y1, 'BLACK', 0.6))
        ops.append(R(x0 + 2.5, y0 + 2.5, x1 - 2.5, y1 - 2.5, 'BLACK', 0.5))
    return ops


def stone_body(variant):
    pale = [R(-15, -15, 15, 15, 'OBJ', 0.42)]
    dark = {'t': [(-15, -15, 15, 4)], 'tl': [(-4, -15, 15, 4)], 'c': [(-4, -15, 15, 15), (-15, -15, -4, 4)],
            'in': [(-15, -15, 15, 4)], 'ltr': [(-5, -15, 5, 4)], 'lr': [(-5, -15, 5, 15)],
            'cren': [(-15, -15, 15, 0), (-5, 0, 5, 15)],
            'check': [(-15, 5, -5, 15), (5, 5, 15, 15), (-5, -5, 5, 5), (-15, -15, -5, -5), (5, -15, 15, -5)],
            'square': [(-15, -15, 15, 15)]}[variant]
    return pale + [R(x0, y0, x1, y1, 'BLACK', 0.75) for (x0, y0, x1, y1) in dark]


def skyline(w, h, steps):
    """Stepped silhouette of blocks fading downwards (deco bricks 113/114)."""
    ops, x = [], -w / 2
    for bw, f in steps:
        ops.append(VG(x, -h / 2, x + bw, -h / 2 + f * h, 'OBJ', 1.0, 0.1))
        x += bw
    return ops


TILE_PROGS = {
    'blk_t': [FULL] + edges('t'), 'blk_tl': [FULL] + edges('tl'), 'blk_c': [FULL] + edges('c'), 'blk_in': [FULL],
    'blk_ltr': [FULL] + edges('ltr'), 'blk_lr': [FULL] + edges('lr'),
    'edge_ltrb': edges('ltrb'), 'edge_t': edges('t'), 'edge_c': edges('c'), 'edge_lr': edges('lr'),
    'brick_all': brick_body() + edges('ltrb'), 'brick_t': brick_body() + edges('t'), 'brick_tl': brick_body() + edges('tl'),
    'brick_c': brick_body() + edges('c'), 'brick_in': brick_body(), 'brick_ltr': brick_body() + edges('ltr'),
    'brick_lr': brick_body() + edges('lr'),
    'stone_t': stone_body('t') + edges('t'), 'stone_tl': stone_body('tl') + edges('tl'), 'stone_c': stone_body('c') + edges('c'),
    'stone_in': stone_body('in'), 'stone_ltr': stone_body('ltr') + edges('ltr'), 'stone_lr': stone_body('lr') + edges('lr'),
    'stone_cren': stone_body('cren') + edges('c'), 'stone_check': stone_body('check') + edges('ltrb'),
    'fake_square': [R(-15, -15, 15, 15, 'BLACK', 0.8)],
    'deco_bricks_l': skyline(200, 46, [(24, 0.45), (30, 1.0), (32, 0.62), (28, 0.35), (36, 0.8), (26, 0.5), (24, 0.3)]),
    'deco_bricks_m': skyline(128, 34, [(20, 0.5), (26, 0.95), (22, 0.6), (30, 0.4), (30, 0.75)]),
    'metal_slab': [R(-15, -10.5, 15, 10.5, 'BLACK', 0.85), R(-15, 4, 15, 9, 'OBJ'), R(-15, 9, 15, 10.5, 'OBJ', 0.6),
                   R(-11, -5.5, -7, -1.5, 'OBJ', 0.8), R(7, -5.5, 11, -1.5, 'OBJ', 0.8)],
    'metal_slab2': [R(-15, -10.5, 15, 10.5, 'BLACK', 0.85), R(-15, 4, 15, 9, 'OBJ'), R(-15, 9, 15, 10.5, 'OBJ', 0.6),
                    R(-11, -5.5, -7, -1.5, 'OBJ', 0.8), R(8, -10.5, 15, 4, 'OBJ', 0.5)],
}


TILE_PROGS.update(objdefs_dl.PROGS)
RADIUS.update({INDEX[n]: r for n, r in objdefs_dl.RADIUS.items()})
RADIUS.update({INDEX[n]: r for n, r in objdefs_dash.RADIUS.items()})
RADIUS.update({INDEX[n]: r for n, r in objdefs_ms.RADIUS.items()})


def _auto_radius(first, last):
    """Dash's objects (and Meltdown's and SubZero's) drawn only by tile
    programs reach as far as their corners (scaled pixel squares must not
    look bigger than they are)."""
    for k, o in enumerate(OBJECTS[first:last], first + 1):
        if k in RADIUS:
            continue
        parts, r = o[7], math.hypot(o[3], o[4]) / 2
        if parts and not all(isinstance(p[0], str) and p[0].startswith('prog:') for p in parts):
            continue
        for p in parts:
            for op in TILE_PROGS[p[0][5:]]:
                pts = op[1] if op[0] == 'poly' else [(op[1], op[2]), (op[3], op[4])]
                for (x, y) in pts:
                    r = max(r, math.hypot(abs(x) + abs(p[1]), abs(y) + abs(p[2])))
        RADIUS[k] = max(1, int(math.ceil(r)))


_auto_radius(EXTRA_FIRST - 1 - len(objdefs_dash.OBJS), EXTRA_FIRST - 1)
_auto_radius(EXTRA_FIRST - 1, len(OBJECTS))

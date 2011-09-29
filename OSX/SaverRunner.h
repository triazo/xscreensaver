/* xscreensaver, Copyright (c) 2006-2011 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#import <Cocoa/Cocoa.h>
#import <ScreenSaver/ScreenSaver.h>

@interface SaverRunner : NSObject
{
  NSString *saverDir;
  NSArray  *saverNames;
  NSArray  *windows;
  NSBundle *saverBundle;
  IBOutlet NSMenu *menubar;
}

- (IBAction) openPreferences: (id)sender;
- (IBAction) aboutPanel: (id)sender;

@end
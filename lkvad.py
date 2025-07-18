#!/usr/bin/env python3
import argparse
import requests
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlparse
import xml.etree.ElementTree as ET

def verify_url(url, timeout=5):
    """Check if a URL is accessible."""
    try:
        response = requests.head(url, timeout=timeout, allow_redirects=True)
        return 200 <= response.status_code < 400
    except:
        return False

def generate_playlist(args):
    """Generate playlist based on arguments."""
    # Parse template
    if '*' not in args.link:
        raise ValueError("Template must contain wildcard (*)")
    
    prefix, suffix = args.link.split('*', 1)
    
    # Generate URLs
    urls = []
    for i in range(args.start, args.end + 1):
        if args.padding:
            num_str = str(i).zfill(args.padding)
        else:
            num_str = str(i)
        
        url = f"{prefix}{num_str}{suffix}"
        if args.prefix:
            url = args.prefix + url
        if args.suffix:
            url = url + args.suffix
            
        urls.append((i, url))
    
    # Verify URLs if requested
    if args.verify:
        print("Verifying URLs...")
        valid_urls = []
        with ThreadPoolExecutor(max_workers=args.threads) as executor:
            results = list(executor.map(lambda u: (u[0], u[1], verify_url(u[1])), urls))
            valid_urls = [(i, url) for i, url, valid in results if valid]
            
        print(f"Found {len(valid_urls)} valid URLs out of {len(urls)}")
        urls = valid_urls
    
    # Write playlist
    with open(args.playlist, 'w') as f:
        if args.format in ['m3u', 'm3u8']:
            f.write("#EXTM3U\n")
            for i, url in urls:
                f.write(f"#EXTINF:-1,Track {i}\n")
                f.write(f"{url}\n")
        
        elif args.format == 'pls':
            f.write("[playlist]\n")
            f.write(f"NumberOfEntries={len(urls)}\n")
            f.write("Version=2\n\n")
            for idx, (i, url) in enumerate(urls, 1):
                f.write(f"File{idx}={url}\n")
                f.write(f"Title{idx}=Track {i}\n")
                f.write(f"Length{idx}=-1\n\n")
        
        elif args.format == 'xspf':
            root = ET.Element('playlist', version='1', xmlns='http://xspf.org/ns/0/')
            tracklist = ET.SubElement(root, 'trackList')
            for i, url in urls:
                track = ET.SubElement(tracklist, 'track')
                ET.SubElement(track, 'location').text = url
                ET.SubElement(track, 'title').text = f'Track {i}'
            
            tree = ET.ElementTree(root)
            tree.write(args.playlist, encoding='UTF-8', xml_declaration=True)
        
        else:  # plain
            for _, url in urls:
                f.write(f"{url}\n")
    
    print(f"Playlist saved to {args.playlist}")

def main():
    parser = argparse.ArgumentParser(description='Enhanced Playlist Generator')
    parser.add_argument('-l', '--link', required=True, help='URL template with wildcard (*)')
    parser.add_argument('-s', '--start', type=int, required=True, help='Starting number')
    parser.add_argument('-e', '--end', type=int, required=True, help='Ending number')
    parser.add_argument('-p', '--playlist', required=True, help='Output playlist file')
    parser.add_argument('-f', '--format', choices=['plain', 'm3u', 'm3u8', 'pls', 'xspf'], 
                        default='plain', help='Playlist format')
    parser.add_argument('-z', '--padding', type=int, help='Zero-pad numbers')
    parser.add_argument('-v', '--verify', action='store_true', help='Verify URLs')
    parser.add_argument('-t', '--threads', type=int, default=10, help='Threads for verification')
    parser.add_argument('-P', '--prefix', help='URL prefix')
    parser.add_argument('-S', '--suffix', help='URL suffix')
    
    args = parser.parse_args()
    
    if args.start > args.end:
        parser.error("Start value cannot be greater than end value")
    
    generate_playlist(args)

if __name__ == '__main__':
    main()
